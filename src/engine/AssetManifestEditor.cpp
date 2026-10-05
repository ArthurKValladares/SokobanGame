#include "engine/AssetManifestEditor.hpp"

#include "engine/AtomicFile.hpp"
#include "engine/ContentPipeline.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace sokoban {
namespace {

using Json = nlohmann::ordered_json;

bool samePath(
    const std::filesystem::path& left,
    const std::filesystem::path& right)
{
    if (left.empty() || right.empty()) {
        return false;
    }
    std::error_code error;
    const bool equivalent = std::filesystem::equivalent(left, right, error);
    if (!error) {
        return equivalent;
    }
    return left.lexically_normal() == right.lexically_normal();
}

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

} // namespace

std::filesystem::path AssetManifestEditor::soundFilePath(std::string_view file) const
{
    const std::filesystem::path relative(file);
    if (relative.empty() || relative.has_root_path()) {
        throw std::runtime_error("Choose a sound file relative to the assets folder.");
    }
    const auto root = std::filesystem::canonical(filePath_.parent_path());
    const auto absolute = std::filesystem::weakly_canonical(root / relative);
    const auto contained = absolute.lexically_relative(root);
    if (contained.empty() || *contained.begin() == "..") {
        throw std::runtime_error("Sound file escapes the assets folder.");
    }
    if (!std::filesystem::is_regular_file(absolute)) {
        throw std::runtime_error("Sound file is missing: " + relative.string());
    }
    return absolute;
}

bool AssetManifestEditor::chooseSoundFile(
    std::size_t soundIndex,
    std::size_t fileIndex,
    const std::filesystem::path& selected)
{
    try {
        if (soundIndex >= sounds_.size() || fileIndex > sounds_[soundIndex].files.size()) {
            throw std::out_of_range("sound file selection no longer matches the document");
        }
        const auto source = std::filesystem::canonical(selected);
        if (!std::filesystem::is_regular_file(source)) {
            throw std::runtime_error("selected sound is not a regular file");
        }
        const auto root = std::filesystem::canonical(filePath_.parent_path());
        auto relative = source.lexically_relative(root);
        if (relative.empty() || relative.has_root_path() || *relative.begin() == "..") {
            const auto importDirectory = root / "custom/audio";
            const auto canonicalImport = std::filesystem::weakly_canonical(importDirectory);
            const auto contained = canonicalImport.lexically_relative(root);
            if (contained.empty() || contained.has_root_path() || *contained.begin() == "..") {
                throw std::runtime_error("audio import folder escapes the assets folder");
            }
            std::filesystem::create_directories(canonicalImport);
            auto destination = canonicalImport / source.filename();
            for (std::size_t suffix = 2; std::filesystem::exists(destination); ++suffix) {
                destination = canonicalImport /
                    (source.stem().string() + "_" + std::to_string(suffix) + source.extension().string());
            }
            std::filesystem::copy_file(source, destination);
            relative = destination.lexically_relative(root);
        }
        const std::string file = relative.generic_string();
        if (fileIndex == sounds_[soundIndex].files.size()) {
            sounds_[soundIndex].files.push_back(file);
        } else {
            sounds_[soundIndex].files[fileIndex] = file;
        }
        markChanged();
        status_ = "Selected " + file;
        return true;
    } catch (const std::exception& error) {
        status_ = "Sound selection failed: " + std::string(error.what());
        return false;
    }
}

void AssetManifestEditor::initialize(
    std::filesystem::path filePath,
    std::filesystem::path runtimePath)
{
    runtimePath_ = std::move(runtimePath);
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
    try {
        if (filePath_.empty()) {
            throw std::runtime_error("no manifest path is configured");
        }
        const std::string contents = serialize();
        (void)AssetManifest::parse(contents);

        atomicFile::write(filePath_, contents);
        if (!runtimePath_.empty() && !samePath(filePath_, runtimePath_)) {
            // Publish newly selected sounds with the runtime manifest. Draft
            // paths may still be missing, matching the staging policy.
            for (const auto& sound : sounds_) {
                for (const auto& file : sound.files) {
                    if (!std::filesystem::exists(filePath_.parent_path() / file)) {
                        continue;
                    }
                    const auto source = soundFilePath(file);
                    const auto destination = runtimePath_.parent_path() / file;
                    if (!samePath(source, destination)) {
                        std::filesystem::create_directories(destination.parent_path());
                        std::filesystem::copy_file(
                            source, destination, std::filesystem::copy_options::overwrite_existing);
                    }
                }
            }
            atomicFile::write(runtimePath_, contents);
            (void)refreshContentPackageIndex(runtimePath_.parent_path());
        }
        dirty_ = false;
        status_ = "Saved " + filePath_.string();
        return true;
    } catch (const std::exception& error) {
        dirty_ = true;
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
        Json entry {
            { "name", texture.name },
            { "path", texture.path },
        };
        // Only written when set, so manifests that never tile stay unchanged;
        // dropping it here would silently un-tile ground splatting on save.
        // Only write non-defaults, so unrelated manifests round-trip byte for
        // byte. Dropping any of these silently changes how the texture is
        // sampled, which is why they survive an editor save at all.
        if (texture.tiling) {
            entry["tiling"] = true;
        }
        if (texture.filter == TextureFilter::Linear) {
            entry["filter"] = "linear";
        }
        if (texture.colorSpace == TextureColorSpace::Linear) {
            entry["colorSpace"] = "linear";
        }
        root["textures"].push_back(std::move(entry));
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
        if (model.materialMode == ModelMaterialMode::Untextured) {
            item["material"] = {
                { "mode", "none" },
            };
        } else if (model.materialMode == ModelMaterialMode::SingleTexture) {
            item["material"] = {
                { "mode", "texture" },
                { "texture", model.materialTextureName },
            };
        } else if (model.materialMode == ModelMaterialMode::PrimitiveMaterials) {
            Json slots = Json::array();
            for (const AssetManifest::Model::PrimitiveMaterial& material :
                 model.primitiveMaterials) {
                Json slot = {
                    { "texture", material.textureName },
                };
                if (material.scrollV) {
                    slot["scrollV"] = true;
                }
                slots.push_back(std::move(slot));
            }
            item["material"] = {
                { "mode", "primitive-materials" },
                { "slots", std::move(slots) },
            };
        }
        if (model.preserveAspectRatio) {
            item["preserveAspectRatio"] = true;
        }
        if (model.preserveSourceScale) {
            item["preserveSourceScale"] = true;
        }
        if (model.rotateHalfTurn) {
            item["rotateHalfTurn"] = true;
        }
        if (!model.attachments.empty()) {
            Json attachments = Json::array();
            for (const AssetManifest::Model::Attachment& attachment :
                 model.attachments) {
                Json attachmentItem = {
                    { "path", attachment.path },
                    { "node", attachment.node },
                };
                if (attachment.rotateHalfTurn) {
                    attachmentItem["rotateHalfTurn"] = true;
                }
                attachments.push_back(std::move(attachmentItem));
            }
            item["attachments"] = std::move(attachments);
        }
        if (model.playerRole) {
            item["role"] = "player";
        } else if (model.enemyRole) {
            item["role"] = "enemy";
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
