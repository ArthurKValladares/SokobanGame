#include "engine/AssetManifest.hpp"

#include <cstddef>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace sokoban {
namespace {

struct Line {
    std::size_t number = 0;
    std::string_view key;
    std::string_view rest;
};

[[nodiscard]] std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

[[noreturn]] void fail(std::size_t lineNumber, const std::string& message)
{
    throw std::runtime_error(
        "asset manifest line " + std::to_string(lineNumber) + ": " + message);
}

[[nodiscard]] Line splitLine(std::size_t number, std::string_view text)
{
    const std::size_t space = text.find(' ');
    if (space == std::string_view::npos) {
        return { number, text, {} };
    }
    return { number, text.substr(0, space), trim(text.substr(space + 1)) };
}

[[nodiscard]] bool parseBool(const Line& line)
{
    if (line.rest == "true") {
        return true;
    }
    if (line.rest == "false") {
        return false;
    }
    fail(line.number, "expected true or false, got '" + std::string(line.rest) + "'");
}

[[nodiscard]] uint32_t parseUint(const Line& line, std::string_view value)
{
    uint32_t result = 0;
    bool any = false;
    for (char c : value) {
        if (c < '0' || c > '9') {
            fail(line.number, "expected a non-negative integer, got '" + std::string(value) + "'");
        }
        result = result * 10 + static_cast<uint32_t>(c - '0');
        any = true;
    }
    if (!any) {
        fail(line.number, "expected a non-negative integer");
    }
    return result;
}

[[nodiscard]] float parseFloat(const Line& line)
{
    try {
        std::size_t consumed = 0;
        const float result = std::stof(std::string(line.rest), &consumed);
        if (consumed != line.rest.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return result;
    } catch (const std::exception&) {
        fail(line.number, "expected a number, got '" + std::string(line.rest) + "'");
    }
}

[[nodiscard]] std::optional<TileType> tileTypeByName(std::string_view name)
{
    for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
        if (definition.name == name) {
            return definition.type;
        }
    }
    return std::nullopt;
}

} // namespace

AssetManifest AssetManifest::parse(std::string_view text)
{
    AssetManifest manifest;

    enum class Section { None, Texture, Model, Animation, Tile, Sound, Music };
    Section section = Section::None;
    std::size_t sectionIndex = 0;
    std::size_t sectionLine = 0;

    std::size_t lineNumber = 0;
    std::size_t cursor = 0;
    while (cursor <= text.size()) {
        const std::size_t end = text.find('\n', cursor);
        std::string_view raw = text.substr(
            cursor, end == std::string_view::npos ? text.size() - cursor : end - cursor);
        cursor = end == std::string_view::npos ? text.size() + 1 : end + 1;
        ++lineNumber;

        const std::size_t comment = raw.find('#');
        if (comment != std::string_view::npos) {
            raw = raw.substr(0, comment);
        }
        // Unindented lines start sections; indented lines are properties of
        // the current section. This keeps section keywords (like "model")
        // usable as property names.
        const bool indented = !raw.empty() && (raw.front() == ' ' || raw.front() == '\t');
        const std::string_view trimmed = trim(raw);
        if (trimmed.empty()) {
            continue;
        }
        const Line line = splitLine(lineNumber, trimmed);

        if (!indented) {
            if (line.key != "texture" && line.key != "model" && line.key != "animation" &&
                line.key != "tile" && line.key != "sound" && line.key != "music") {
                fail(line.number,
                    "expected a section (texture/model/animation/tile/sound/music), got '" +
                    std::string(line.key) + "' - properties must be indented");
            }
            if (line.rest.empty()) {
                fail(line.number, std::string(line.key) + " requires a name");
            }
            sectionLine = line.number;
            if (line.key == "texture") {
                section = Section::Texture;
                sectionIndex = manifest.textures_.size();
                manifest.textures_.push_back({ std::string(line.rest), {} });
            } else if (line.key == "model") {
                section = Section::Model;
                sectionIndex = manifest.models_.size();
                Model model;
                model.name = std::string(line.rest);
                manifest.models_.push_back(std::move(model));
            } else if (line.key == "animation") {
                section = Section::Animation;
                sectionIndex = manifest.animations_.size();
                Animation animation;
                animation.name = std::string(line.rest);
                manifest.animations_.push_back(std::move(animation));
            } else if (line.key == "tile") {
                const std::optional<TileType> type = tileTypeByName(line.rest);
                if (!type) {
                    fail(line.number, "unknown tile type '" + std::string(line.rest) + "'");
                }
                section = Section::Tile;
                sectionIndex = static_cast<std::size_t>(*type);
            } else if (line.key == "sound") {
                section = Section::Sound;
                sectionIndex = manifest.sounds_.size();
                manifest.sounds_.push_back({ std::string(line.rest), {} });
            } else {
                section = Section::Music;
                sectionIndex = manifest.music_.size();
                manifest.music_.push_back(
                    { static_cast<int>(parseUint(line, line.rest)), {} });
            }
            continue;
        }

        switch (section) {
        case Section::None:
            fail(line.number, "property '" + std::string(line.key) + "' outside of any section");
        case Section::Texture: {
            Texture& texture = manifest.textures_[sectionIndex];
            if (line.key == "path") {
                texture.path = std::string(line.rest);
            } else {
                fail(line.number, "unknown texture property '" + std::string(line.key) + "'");
            }
            break;
        }
        case Section::Model: {
            Model& model = manifest.models_[sectionIndex];
            if (line.key == "path") {
                model.path = std::string(line.rest);
            } else if (line.key == "geometry") {
                if (line.rest == "static") {
                    model.geometry = ModelGeometry::Static;
                } else if (line.rest == "skinned") {
                    model.geometry = ModelGeometry::Skinned;
                } else {
                    fail(line.number, "geometry must be static or skinned");
                }
            } else if (line.key == "material") {
                const Line material = splitLine(line.number, line.rest);
                if (material.key == "none") {
                    model.materialMode = ModelMaterialMode::Untextured;
                } else if (material.key == "texture") {
                    model.materialMode = ModelMaterialMode::SingleTexture;
                    // Resolved to an index in validateAndResolve; stash the name.
                    model.textureIndex = 0;
                    if (material.rest.empty()) {
                        fail(line.number, "material texture requires a texture name");
                    }
                    model.materialTextureName = std::string(material.rest);
                } else if (material.key == "primitive-texture-index") {
                    model.materialMode = ModelMaterialMode::PrimitiveTextureIndex;
                    model.textureIndex = parseUint(line, material.rest);
                } else {
                    fail(line.number,
                        "material must be none, texture <name>, or primitive-texture-index <n>");
                }
            } else if (line.key == "preserve-aspect") {
                model.preserveAspectRatio = parseBool(line);
            } else if (line.key == "rotate-half-turn") {
                model.rotateHalfTurn = parseBool(line);
            } else if (line.key == "primitive-textures") {
                model.primitiveTextures = parseBool(line);
            } else if (line.key == "belt-scroll") {
                model.beltScroll = parseBool(line);
            } else if (line.key == "role") {
                if (line.rest != "player") {
                    fail(line.number, "the only model role is 'player'");
                }
                model.playerRole = true;
            } else {
                fail(line.number, "unknown model property '" + std::string(line.key) + "'");
            }
            break;
        }
        case Section::Animation: {
            Animation& animation = manifest.animations_[sectionIndex];
            if (line.key == "path") {
                animation.path = std::string(line.rest);
            } else if (line.key == "clip") {
                animation.clip = parseUint(line, line.rest);
            } else if (line.key == "role") {
                if (line.rest != "player-idle" && line.rest != "player-move" &&
                    line.rest != "player-push") {
                    fail(line.number,
                        "animation role must be player-idle, player-move, or player-push");
                }
                animation.role = std::string(line.rest);
            } else {
                fail(line.number, "unknown animation property '" + std::string(line.key) + "'");
            }
            break;
        }
        case Section::Tile: {
            TileVisual& visual = manifest.tileVisuals_[sectionIndex];
            if (line.key == "model") {
                // Resolved to an id in validateAndResolve; stash the name.
                manifest.tileModelNames_[sectionIndex] = { std::string(line.rest), line.number };
            } else if (line.key == "scale") {
                visual.scale = parseFloat(line);
            } else {
                fail(line.number, "unknown tile property '" + std::string(line.key) + "'");
            }
            break;
        }
        case Section::Sound: {
            if (line.key == "file") {
                manifest.sounds_[sectionIndex].files.push_back(std::string(line.rest));
            } else {
                fail(line.number, "unknown sound property '" + std::string(line.key) + "'");
            }
            break;
        }
        case Section::Music: {
            if (line.key == "file") {
                manifest.music_[sectionIndex].file = std::string(line.rest);
            } else {
                fail(line.number, "unknown music property '" + std::string(line.key) + "'");
            }
            break;
        }
        }
        (void)sectionLine;
    }

    manifest.validateAndResolve();
    return manifest;
}

AssetManifest AssetManifest::loadFromFile(const std::filesystem::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open asset manifest: " + file.string());
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return parse(buffer.str());
}

void AssetManifest::validateAndResolve()
{
    const auto duplicate = [](const auto& items, const auto& name) {
        std::size_t count = 0;
        for (const auto& item : items) {
            count += item.name == name ? 1 : 0;
        }
        return count > 1;
    };

    for (const Texture& texture : textures_) {
        if (texture.path.empty()) {
            throw std::runtime_error("asset manifest: texture '" + texture.name + "' has no path");
        }
        if (duplicate(textures_, texture.name)) {
            throw std::runtime_error("asset manifest: duplicate texture '" + texture.name + "'");
        }
    }
    if (textures_.size() > maxModelTextures) {
        throw std::runtime_error(
            "asset manifest: too many textures (max " + std::to_string(maxModelTextures) + ")");
    }

    for (Model& model : models_) {
        if (model.path.empty()) {
            throw std::runtime_error("asset manifest: model '" + model.name + "' has no path");
        }
        if (duplicate(models_, model.name)) {
            throw std::runtime_error("asset manifest: duplicate model '" + model.name + "'");
        }
        if (model.materialMode == ModelMaterialMode::SingleTexture) {
            bool found = false;
            for (std::size_t i = 0; i < textures_.size(); ++i) {
                if (textures_[i].name == model.materialTextureName) {
                    model.textureIndex = static_cast<uint32_t>(i);
                    found = true;
                    break;
                }
            }
            if (!found) {
                throw std::runtime_error(
                    "asset manifest: model '" + model.name + "' references unknown texture '" +
                    model.materialTextureName + "'");
            }
        }
        if (model.materialMode == ModelMaterialMode::PrimitiveTextureIndex &&
            model.textureIndex >= textures_.size()) {
            throw std::runtime_error(
                "asset manifest: model '" + model.name + "' texture index out of range");
        }
        if (model.playerRole) {
            if (!playerModel_.isCube()) {
                throw std::runtime_error("asset manifest: multiple models with role player");
            }
            if (model.geometry != ModelGeometry::Skinned) {
                throw std::runtime_error(
                    "asset manifest: player model '" + model.name + "' must be skinned");
            }
            playerModel_ = modelIdByName(model.name);
        }
    }

    for (const Animation& animation : animations_) {
        if (animation.path.empty()) {
            throw std::runtime_error(
                "asset manifest: animation '" + animation.name + "' has no path");
        }
        if (duplicate(animations_, animation.name)) {
            throw std::runtime_error(
                "asset manifest: duplicate animation '" + animation.name + "'");
        }
        RenderAnimation* role = nullptr;
        if (animation.role == "player-idle") {
            role = &playerIdle_;
        } else if (animation.role == "player-move") {
            role = &playerMove_;
        } else if (animation.role == "player-push") {
            role = &playerPush_;
        }
        if (role != nullptr) {
            if (!role->isNone()) {
                throw std::runtime_error(
                    "asset manifest: duplicate animation role '" + animation.role + "'");
            }
            *role = animationIdByName(animation.name);
        }
    }

    if (playerModel_.isCube()) {
        throw std::runtime_error("asset manifest: no model has role player");
    }
    if (playerIdle_.isNone() || playerMove_.isNone() || playerPush_.isNone()) {
        throw std::runtime_error(
            "asset manifest: animations with roles player-idle, player-move, and "
            "player-push are all required");
    }

    for (std::size_t i = 0; i < tileModelNames_.size(); ++i) {
        if (tileModelNames_[i].name.empty()) {
            continue;
        }
        try {
            tileVisuals_[i].model = modelIdByName(tileModelNames_[i].name);
        } catch (const std::exception&) {
            fail(tileModelNames_[i].line,
                "tile references unknown model '" + tileModelNames_[i].name + "'");
        }
    }

    for (const SoundSet& set : sounds_) {
        if (set.files.empty()) {
            throw std::runtime_error("asset manifest: sound set '" + set.name + "' has no files");
        }
        if (duplicate(sounds_, set.name)) {
            throw std::runtime_error("asset manifest: duplicate sound set '" + set.name + "'");
        }
    }

    for (const MusicTrack& track : music_) {
        if (track.file.empty()) {
            throw std::runtime_error(
                "asset manifest: music for level " + std::to_string(track.level) + " has no file");
        }
        std::size_t count = 0;
        for (const MusicTrack& other : music_) {
            count += other.level == track.level ? 1 : 0;
        }
        if (count > 1) {
            throw std::runtime_error(
                "asset manifest: multiple music entries for level " + std::to_string(track.level));
        }
    }
}

RenderModel AssetManifest::modelIdByName(std::string_view name) const
{
    for (std::size_t i = 0; i < models_.size(); ++i) {
        if (models_[i].name == name) {
            return RenderModel { static_cast<uint32_t>(i + 1) };
        }
    }
    throw std::runtime_error("asset manifest: unknown model '" + std::string(name) + "'");
}

RenderAnimation AssetManifest::animationIdByName(std::string_view name) const
{
    for (std::size_t i = 0; i < animations_.size(); ++i) {
        if (animations_[i].name == name) {
            return RenderAnimation { static_cast<uint32_t>(i + 1) };
        }
    }
    throw std::runtime_error("asset manifest: unknown animation '" + std::string(name) + "'");
}

const AssetManifest::Model& AssetManifest::model(RenderModel id) const
{
    if (id.isCube() || id.index() >= models_.size()) {
        throw std::out_of_range("asset manifest: invalid model id");
    }
    return models_[id.index()];
}

const AssetManifest::Animation& AssetManifest::animation(RenderAnimation id) const
{
    if (id.isNone() || id.index() >= animations_.size()) {
        throw std::out_of_range("asset manifest: invalid animation id");
    }
    return animations_[id.index()];
}

const AssetManifest::TileVisual& AssetManifest::tileVisual(TileType type) const
{
    return tileVisuals_[static_cast<std::size_t>(type)];
}

const std::vector<std::string>& AssetManifest::soundSet(std::string_view name) const
{
    static const std::vector<std::string> empty;
    for (const SoundSet& set : sounds_) {
        if (set.name == name) {
            return set.files;
        }
    }
    return empty;
}

const std::string* AssetManifest::musicForLevel(int level) const
{
    for (const MusicTrack& track : music_) {
        if (track.level == level) {
            return &track.file;
        }
    }
    return nullptr;
}

} // namespace sokoban
