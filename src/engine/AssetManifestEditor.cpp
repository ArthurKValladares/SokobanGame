#include "engine/AssetManifestEditor.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace sokoban {
namespace {

using Json = nlohmann::ordered_json;

template <typename Item>
std::string uniqueName(
    std::string base,
    const std::vector<Item>& items)
{
    auto isAvailable = [&](const std::string& candidate) {
        return std::ranges::none_of(items, [&](const Item& item) {
            return item.name == candidate;
        });
    };
    if (isAvailable(base)) {
        return base;
    }
    for (std::size_t suffix = 2;; ++suffix) {
        std::string candidate = base + std::to_string(suffix);
        if (isAvailable(candidate)) {
            return candidate;
        }
    }
}

template <typename Item>
bool removeItem(std::vector<Item>& items, std::size_t index)
{
    if (index >= items.size()) {
        return false;
    }
    items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

template <typename Item>
bool moveItem(std::vector<Item>& items, std::size_t index, int direction)
{
    if (index >= items.size() || direction == 0) {
        return false;
    }
    if (direction < 0) {
        if (index == 0) {
            return false;
        }
        std::swap(items[index], items[index - 1]);
        return true;
    }
    if (index + 1 >= items.size()) {
        return false;
    }
    std::swap(items[index], items[index + 1]);
    return true;
}

template <typename Item>
void updateItem(std::vector<Item>& items, std::size_t index, Item item)
{
    if (index >= items.size()) {
        throw std::out_of_range("asset manifest editor item index out of range");
    }
    items[index] = std::move(item);
}

void replaceFile(
    const std::filesystem::path& destination,
    const std::filesystem::path& temporary)
{
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (!error) {
        return;
    }

    const std::filesystem::path backup = destination.string() + ".bak";
    std::filesystem::remove(backup, error);
    error.clear();
    std::filesystem::rename(destination, backup, error);
    if (error) {
        throw std::runtime_error(
            "cannot prepare manifest replacement: " + error.message());
    }

    error.clear();
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::error_code rollbackError;
        std::filesystem::rename(backup, destination, rollbackError);
        throw std::runtime_error(
            "cannot replace manifest: " + error.message());
    }
    std::filesystem::remove(backup, error);
}

} // namespace

void AssetManifestEditor::initialize(std::filesystem::path filePath)
{
    (void)load(std::move(filePath));
}

bool AssetManifestEditor::load(std::filesystem::path filePath)
{
    try {
        const AssetManifest manifest = AssetManifest::loadFromFile(filePath);
        filePath_ = std::move(filePath);
        textures_ = manifest.textures();
        models_ = manifest.models();
        animations_ = manifest.animations();
        tiles_ = manifest.tileEntries();
        sounds_ = manifest.soundSets();
        music_ = manifest.musicTracks();
        dirty_ = false;
        status_ = "Loaded " + filePath_.string();
        return true;
    } catch (const std::exception& error) {
        status_ = "Load failed: " + std::string(error.what());
        return false;
    }
}

bool AssetManifestEditor::reload()
{
    if (filePath_.empty()) {
        status_ = "Reload failed: no manifest path is configured.";
        return false;
    }
    return load(filePath_);
}

bool AssetManifestEditor::validate()
{
    try {
        (void)AssetManifest::parse(serialize());
        status_ = "Manifest is valid.";
        return true;
    } catch (const std::exception& error) {
        status_ = "Validation failed: " + std::string(error.what());
        return false;
    }
}

bool AssetManifestEditor::save()
{
    const std::filesystem::path temporary = filePath_.string() + ".tmp";
    try {
        if (filePath_.empty()) {
            throw std::runtime_error("no manifest path is configured");
        }
        const std::string contents = serialize();
        (void)AssetManifest::parse(contents);

        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error(
                "cannot open temporary manifest: " + temporary.string());
        }
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        stream.close();
        if (!stream) {
            throw std::runtime_error(
                "cannot write temporary manifest: " + temporary.string());
        }

        replaceFile(filePath_, temporary);
        dirty_ = false;
        status_ = "Saved " + filePath_.string();
        return true;
    } catch (const std::exception& error) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        status_ = "Save failed: " + std::string(error.what());
        return false;
    }
}

std::string AssetManifestEditor::serialize() const
{
    Json root = Json::object();
    root["format"] = 1;

    root["textures"] = Json::array();
    for (const AssetManifest::Texture& texture : textures_) {
        root["textures"].push_back({
            { "name", texture.name },
            { "path", texture.path },
        });
    }

    root["models"] = Json::array();
    for (const AssetManifest::Model& model : models_) {
        Json item = {
            { "name", model.name },
            { "path", model.path },
        };
        if (model.geometry == ModelGeometry::Skinned) {
            item["geometry"] = "skinned";
        }
        if (model.materialMode == ModelMaterialMode::SingleTexture) {
            item["material"] = {
                { "mode", "texture" },
                { "texture", model.materialTextureName },
            };
        } else if (model.materialMode == ModelMaterialMode::PrimitiveTextureIndex) {
            item["material"] = {
                { "mode", "primitive-texture-index" },
                { "index", model.textureIndex },
            };
        }
        if (model.preserveAspectRatio) {
            item["preserveAspectRatio"] = true;
        }
        if (model.rotateHalfTurn) {
            item["rotateHalfTurn"] = true;
        }
        if (model.beltScroll) {
            item["beltScroll"] = true;
        }
        if (model.playerRole) {
            item["role"] = "player";
        }
        root["models"].push_back(std::move(item));
    }

    root["animations"] = Json::array();
    for (const AssetManifest::Animation& animation : animations_) {
        Json item = {
            { "name", animation.name },
            { "path", animation.path },
        };
        if (animation.clip != 0) {
            item["clip"] = animation.clip;
        }
        if (!animation.role.empty()) {
            item["role"] = animation.role;
        }
        root["animations"].push_back(std::move(item));
    }

    root["tiles"] = Json::array();
    for (const AssetManifest::TileEntry& tile : tiles_) {
        Json item = { { "tile", tileTypeName(tile.tile) } };
        if (!tile.modelName.empty()) {
            item["model"] = tile.modelName;
        }
        if (tile.scale != 1.0f) {
            item["scale"] = tile.scale;
        }
        root["tiles"].push_back(std::move(item));
    }

    root["sounds"] = Json::array();
    for (const AssetManifest::SoundSet& sound : sounds_) {
        Json item = {
            { "name", sound.name },
            { "files", sound.files },
        };
        if (sound.volume != 1.0f) {
            item["volume"] = sound.volume;
        }
        root["sounds"].push_back(std::move(item));
    }

    root["music"] = Json::array();
    for (const AssetManifest::MusicTrack& track : music_) {
        Json item = {
            { "level", track.level },
            { "file", track.file },
        };
        if (track.volume != 1.0f) {
            item["volume"] = track.volume;
        }
        root["music"].push_back(std::move(item));
    }

    return root.dump(2) + '\n';
}

void AssetManifestEditor::markChanged()
{
    dirty_ = true;
    status_.clear();
}

void AssetManifestEditor::updateTexture(std::size_t index, AssetManifest::Texture texture)
{
    updateItem(textures_, index, std::move(texture));
    markChanged();
}

void AssetManifestEditor::updateModel(std::size_t index, AssetManifest::Model model)
{
    updateItem(models_, index, std::move(model));
    markChanged();
}

void AssetManifestEditor::updateAnimation(std::size_t index, AssetManifest::Animation animation)
{
    updateItem(animations_, index, std::move(animation));
    markChanged();
}

void AssetManifestEditor::updateTile(std::size_t index, AssetManifest::TileEntry tile)
{
    updateItem(tiles_, index, std::move(tile));
    markChanged();
}

void AssetManifestEditor::updateSoundSet(std::size_t index, AssetManifest::SoundSet sound)
{
    updateItem(sounds_, index, std::move(sound));
    markChanged();
}

void AssetManifestEditor::updateMusicTrack(std::size_t index, AssetManifest::MusicTrack track)
{
    updateItem(music_, index, std::move(track));
    markChanged();
}

void AssetManifestEditor::addTexture()
{
    textures_.push_back({ uniqueName("Texture", textures_), "textures/texture.png" });
    markChanged();
}

void AssetManifestEditor::addModel()
{
    AssetManifest::Model model;
    model.name = uniqueName("Model", models_);
    model.path = "models/model.gltf";
    models_.push_back(std::move(model));
    markChanged();
}

void AssetManifestEditor::addAnimation()
{
    AssetManifest::Animation animation;
    animation.name = uniqueName("Animation", animations_);
    animation.path = "animations/animation.glb";
    animations_.push_back(std::move(animation));
    markChanged();
}

void AssetManifestEditor::addTile()
{
    TileType type = TileType::Ground;
    for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
        const bool used = std::ranges::any_of(tiles_, [&](const AssetManifest::TileEntry& tile) {
            return tile.tile == definition.type;
        });
        if (!used) {
            type = definition.type;
            break;
        }
    }
    tiles_.push_back({ .tile = type });
    markChanged();
}

void AssetManifestEditor::addSoundSet()
{
    sounds_.push_back({
        .name = uniqueName("sound", sounds_),
        .files = { "audio/sound.ogg" },
    });
    markChanged();
}

void AssetManifestEditor::addMusicTrack()
{
    int level = 0;
    while (std::ranges::any_of(music_, [&](const AssetManifest::MusicTrack& track) {
        return track.level == level;
    })) {
        ++level;
    }
    music_.push_back({ .level = level, .file = "audio/music.ogg" });
    markChanged();
}

bool AssetManifestEditor::removeTexture(std::size_t index)
{
    if (!removeItem(textures_, index)) {
        return false;
    }
    markChanged();
    return true;
}

bool AssetManifestEditor::removeModel(std::size_t index)
{
    if (!removeItem(models_, index)) {
        return false;
    }
    markChanged();
    return true;
}

bool AssetManifestEditor::removeAnimation(std::size_t index)
{
    if (!removeItem(animations_, index)) {
        return false;
    }
    markChanged();
    return true;
}

bool AssetManifestEditor::removeTile(std::size_t index)
{
    if (!removeItem(tiles_, index)) {
        return false;
    }
    markChanged();
    return true;
}

bool AssetManifestEditor::removeSoundSet(std::size_t index)
{
    if (!removeItem(sounds_, index)) {
        return false;
    }
    markChanged();
    return true;
}

bool AssetManifestEditor::removeMusicTrack(std::size_t index)
{
    if (!removeItem(music_, index)) {
        return false;
    }
    markChanged();
    return true;
}

bool AssetManifestEditor::moveTexture(std::size_t index, int direction)
{
    if (!moveItem(textures_, index, direction)) {
        return false;
    }
    markChanged();
    return true;
}

bool AssetManifestEditor::moveModel(std::size_t index, int direction)
{
    if (!moveItem(models_, index, direction)) {
        return false;
    }
    markChanged();
    return true;
}

bool AssetManifestEditor::moveAnimation(std::size_t index, int direction)
{
    if (!moveItem(animations_, index, direction)) {
        return false;
    }
    markChanged();
    return true;
}

bool AssetManifestEditor::moveTile(std::size_t index, int direction)
{
    if (!moveItem(tiles_, index, direction)) {
        return false;
    }
    markChanged();
    return true;
}

bool AssetManifestEditor::moveSoundSet(std::size_t index, int direction)
{
    if (!moveItem(sounds_, index, direction)) {
        return false;
    }
    markChanged();
    return true;
}

bool AssetManifestEditor::moveMusicTrack(std::size_t index, int direction)
{
    if (!moveItem(music_, index, direction)) {
        return false;
    }
    markChanged();
    return true;
}

} // namespace sokoban
