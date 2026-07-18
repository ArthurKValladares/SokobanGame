#include "engine/AssetManifest.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace sokoban {
namespace {

using Json = nlohmann::json;

[[noreturn]] void fail(std::string_view context, const std::string& message)
{
    throw std::runtime_error(
        "asset manifest " + std::string(context) + ": " + message);
}

void requireObject(const Json& value, std::string_view context)
{
    if (!value.is_object()) {
        fail(context, "expected an object");
    }
}

void rejectUnknownProperties(
    const Json& object,
    std::initializer_list<std::string_view> allowed,
    std::string_view context)
{
    requireObject(object, context);
    for (const auto& [key, value] : object.items()) {
        (void)value;
        bool known = false;
        for (const std::string_view candidate : allowed) {
            if (key == candidate) {
                known = true;
                break;
            }
        }
        if (!known) {
            fail(context, "unknown property '" + key + "'");
        }
    }
}

const Json& requiredProperty(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const auto it = object.find(std::string(key));
    if (it == object.end()) {
        fail(context, "missing required property '" + std::string(key) + "'");
    }
    return *it;
}

const Json& optionalArray(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    static const Json empty = Json::array();
    const auto it = object.find(std::string(key));
    if (it == object.end()) {
        return empty;
    }
    if (!it->is_array()) {
        fail(context, "property '" + std::string(key) + "' must be an array");
    }
    return *it;
}

std::string requiredString(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const Json& value = requiredProperty(object, key, context);
    if (!value.is_string()) {
        fail(context, "property '" + std::string(key) + "' must be a string");
    }
    const std::string result = value.get<std::string>();
    if (result.empty()) {
        fail(context, "property '" + std::string(key) + "' must not be empty");
    }
    return result;
}

std::optional<std::string> optionalString(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const auto it = object.find(std::string(key));
    if (it == object.end()) {
        return std::nullopt;
    }
    if (!it->is_string()) {
        fail(context, "property '" + std::string(key) + "' must be a string");
    }
    const std::string result = it->get<std::string>();
    if (result.empty()) {
        fail(context, "property '" + std::string(key) + "' must not be empty");
    }
    return result;
}

bool optionalBool(
    const Json& object,
    std::string_view key,
    bool fallback,
    std::string_view context)
{
    const auto it = object.find(std::string(key));
    if (it == object.end()) {
        return fallback;
    }
    if (!it->is_boolean()) {
        fail(context, "property '" + std::string(key) + "' must be a boolean");
    }
    return it->get<bool>();
}

float optionalFloat(
    const Json& object,
    std::string_view key,
    float fallback,
    std::string_view context)
{
    const auto it = object.find(std::string(key));
    if (it == object.end()) {
        return fallback;
    }
    if (!it->is_number()) {
        fail(context, "property '" + std::string(key) + "' must be a number");
    }
    const float result = it->get<float>();
    if (!std::isfinite(result)) {
        fail(context, "property '" + std::string(key) + "' must be finite");
    }
    return result;
}

uint32_t optionalUint(
    const Json& object,
    std::string_view key,
    uint32_t fallback,
    std::string_view context)
{
    const auto it = object.find(std::string(key));
    if (it == object.end()) {
        return fallback;
    }
    if (!it->is_number_integer()) {
        fail(context, "property '" + std::string(key) + "' must be a non-negative integer");
    }

    if (it->is_number_unsigned()) {
        const uint64_t value = it->get<uint64_t>();
        if (value > std::numeric_limits<uint32_t>::max()) {
            fail(context, "property '" + std::string(key) + "' is out of range");
        }
        return static_cast<uint32_t>(value);
    }

    const int64_t value = it->get<int64_t>();
    if (value < 0 || value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
        fail(context, "property '" + std::string(key) + "' must be a non-negative integer");
    }
    return static_cast<uint32_t>(value);
}

int requiredNonNegativeInt(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const Json& value = requiredProperty(object, key, context);
    if (!value.is_number_integer()) {
        fail(context, "property '" + std::string(key) + "' must be a non-negative integer");
    }

    if (value.is_number_unsigned()) {
        const uint64_t integer = value.get<uint64_t>();
        if (integer > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            fail(context, "property '" + std::string(key) + "' is out of range");
        }
        return static_cast<int>(integer);
    }

    const int64_t integer = value.get<int64_t>();
    if (integer < 0 || integer > std::numeric_limits<int>::max()) {
        fail(context, "property '" + std::string(key) + "' must be a non-negative integer");
    }
    return static_cast<int>(integer);
}

std::optional<TileType> tileTypeByName(std::string_view name)
{
    for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
        if (definition.name == name) {
            return definition.type;
        }
    }
    return std::nullopt;
}

std::string indexedContext(std::string_view array, std::size_t index)
{
    return std::string(array) + "[" + std::to_string(index) + "]";
}

} // namespace

struct AssetManifestJsonParser {
static void parseTextures(const Json& root, AssetManifest& manifest)
{
    const Json& textures = optionalArray(root, "textures", "root");
    manifest.textures_.reserve(textures.size());
    for (std::size_t i = 0; i < textures.size(); ++i) {
        const std::string context = indexedContext("textures", i);
        const Json& texture = textures[i];
        rejectUnknownProperties(texture, { "name", "path" }, context);
        manifest.textures_.push_back({
            requiredString(texture, "name", context),
            requiredString(texture, "path", context),
        });
    }
}

static void parseModelMaterial(
    const Json& modelJson,
    AssetManifest::Model& model,
    std::string_view context)
{
    const auto materialIt = modelJson.find("material");
    if (materialIt == modelJson.end()) {
        return;
    }

    const std::string materialContext = std::string(context) + ".material";
    const Json& material = *materialIt;
    rejectUnknownProperties(material, { "mode", "texture", "index" }, materialContext);
    const std::string mode = requiredString(material, "mode", materialContext);
    const bool hasTexture = material.contains("texture");
    const bool hasIndex = material.contains("index");

    if (mode == "none") {
        if (hasTexture || hasIndex) {
            fail(materialContext, "mode 'none' does not accept texture or index");
        }
    } else if (mode == "texture") {
        if (hasIndex) {
            fail(materialContext, "mode 'texture' does not accept index");
        }
        model.materialMode = ModelMaterialMode::SingleTexture;
        model.materialTextureName = requiredString(material, "texture", materialContext);
    } else if (mode == "primitive-texture-index") {
        if (hasTexture) {
            fail(materialContext, "mode 'primitive-texture-index' does not accept texture");
        }
        if (!hasIndex) {
            fail(materialContext, "mode 'primitive-texture-index' requires index");
        }
        model.materialMode = ModelMaterialMode::PrimitiveTextureIndex;
        model.textureIndex = optionalUint(material, "index", 0, materialContext);
        model.primitiveTextures = true;
    } else {
        fail(materialContext,
            "mode must be 'none', 'texture', or 'primitive-texture-index'");
    }
}

static void parseModels(const Json& root, AssetManifest& manifest)
{
    const Json& models = optionalArray(root, "models", "root");
    manifest.models_.reserve(models.size());
    for (std::size_t i = 0; i < models.size(); ++i) {
        const std::string context = indexedContext("models", i);
        const Json& modelJson = models[i];
        rejectUnknownProperties(modelJson, {
            "name", "path", "geometry", "material", "preserveAspectRatio",
            "rotateHalfTurn", "beltScroll", "role",
        }, context);

        AssetManifest::Model model;
        model.name = requiredString(modelJson, "name", context);
        model.path = requiredString(modelJson, "path", context);
        const std::string geometry = optionalString(modelJson, "geometry", context).value_or("static");
        if (geometry == "static") {
            model.geometry = ModelGeometry::Static;
        } else if (geometry == "skinned") {
            model.geometry = ModelGeometry::Skinned;
        } else {
            fail(context, "geometry must be 'static' or 'skinned'");
        }

        model.preserveAspectRatio = optionalBool(
            modelJson, "preserveAspectRatio", false, context);
        model.rotateHalfTurn = optionalBool(
            modelJson, "rotateHalfTurn", false, context);
        model.beltScroll = optionalBool(modelJson, "beltScroll", false, context);
        parseModelMaterial(modelJson, model, context);

        const std::optional<std::string> role = optionalString(modelJson, "role", context);
        if (role) {
            if (*role != "player") {
                fail(context, "the only model role is 'player'");
            }
            model.playerRole = true;
        }
        manifest.models_.push_back(std::move(model));
    }
}

static void parseAnimations(const Json& root, AssetManifest& manifest)
{
    const Json& animations = optionalArray(root, "animations", "root");
    manifest.animations_.reserve(animations.size());
    for (std::size_t i = 0; i < animations.size(); ++i) {
        const std::string context = indexedContext("animations", i);
        const Json& animationJson = animations[i];
        rejectUnknownProperties(animationJson, { "name", "path", "clip", "role" }, context);

        AssetManifest::Animation animation;
        animation.name = requiredString(animationJson, "name", context);
        animation.path = requiredString(animationJson, "path", context);
        animation.clip = optionalUint(animationJson, "clip", 0, context);
        animation.role = optionalString(animationJson, "role", context).value_or("");
        if (!animation.role.empty() &&
            animation.role != "player-idle" &&
            animation.role != "player-move" &&
            animation.role != "player-push") {
            fail(context,
                "role must be 'player-idle', 'player-move', or 'player-push'");
        }
        manifest.animations_.push_back(std::move(animation));
    }
}

static void parseTiles(const Json& root, AssetManifest& manifest)
{
    const Json& tiles = optionalArray(root, "tiles", "root");
    std::array<bool, tileTypeCount> configured {};
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const std::string context = indexedContext("tiles", i);
        const Json& tileJson = tiles[i];
        rejectUnknownProperties(tileJson, { "tile", "model", "scale" }, context);
        const std::string tileName = requiredString(tileJson, "tile", context);
        const std::optional<TileType> type = tileTypeByName(tileName);
        if (!type) {
            fail(context, "unknown tile type '" + tileName + "'");
        }
        const std::size_t index = static_cast<std::size_t>(*type);
        if (configured[index]) {
            fail(context, "duplicate tile type '" + tileName + "'");
        }
        configured[index] = true;

        manifest.tileVisuals_[index].scale = optionalFloat(tileJson, "scale", 1.0f, context);
        manifest.tileModelNames_[index] = optionalString(tileJson, "model", context).value_or("");
    }
}

static void parseSounds(const Json& root, AssetManifest& manifest)
{
    const Json& sounds = optionalArray(root, "sounds", "root");
    manifest.sounds_.reserve(sounds.size());
    for (std::size_t i = 0; i < sounds.size(); ++i) {
        const std::string context = indexedContext("sounds", i);
        const Json& soundJson = sounds[i];
        rejectUnknownProperties(soundJson, { "name", "files", "volume" }, context);

        AssetManifest::SoundSet sound;
        sound.name = requiredString(soundJson, "name", context);
        sound.volume = optionalFloat(soundJson, "volume", 1.0f, context);
        if (sound.volume < 0.0f) {
            fail(context, "volume must not be negative");
        }
        const Json& files = requiredProperty(soundJson, "files", context);
        if (!files.is_array()) {
            fail(context, "property 'files' must be an array");
        }
        for (std::size_t fileIndex = 0; fileIndex < files.size(); ++fileIndex) {
            if (!files[fileIndex].is_string() || files[fileIndex].get_ref<const std::string&>().empty()) {
                fail(context,
                    "files[" + std::to_string(fileIndex) + "] must be a non-empty string");
            }
            sound.files.push_back(files[fileIndex].get<std::string>());
        }
        manifest.sounds_.push_back(std::move(sound));
    }
}

static void parseMusic(const Json& root, AssetManifest& manifest)
{
    const Json& music = optionalArray(root, "music", "root");
    manifest.music_.reserve(music.size());
    for (std::size_t i = 0; i < music.size(); ++i) {
        const std::string context = indexedContext("music", i);
        const Json& musicJson = music[i];
        rejectUnknownProperties(musicJson, { "level", "file", "volume" }, context);

        AssetManifest::MusicTrack track;
        track.level = requiredNonNegativeInt(musicJson, "level", context);
        track.file = requiredString(musicJson, "file", context);
        track.volume = optionalFloat(musicJson, "volume", 1.0f, context);
        if (track.volume < 0.0f) {
            fail(context, "volume must not be negative");
        }
        manifest.music_.push_back(std::move(track));
    }
}
};

AssetManifest AssetManifest::parse(std::string_view text)
{
    Json root;
    try {
        root = Json::parse(text);
    } catch (const Json::parse_error& error) {
        throw std::runtime_error(
            "asset manifest JSON parse error at byte " +
            std::to_string(error.byte) + ": " + error.what());
    }

    rejectUnknownProperties(root, {
        "format", "textures", "models", "animations", "tiles", "sounds", "music",
    }, "root");
    const Json& format = requiredProperty(root, "format", "root");
    const bool formatIsOne = format.is_number_unsigned()
        ? format.get<uint64_t>() == 1
        : format.is_number_integer() && format.get<int64_t>() == 1;
    if (!formatIsOne) {
        fail("root", "property 'format' must be the integer 1");
    }

    AssetManifest manifest;
    AssetManifestJsonParser::parseTextures(root, manifest);
    AssetManifestJsonParser::parseModels(root, manifest);
    AssetManifestJsonParser::parseAnimations(root, manifest);
    AssetManifestJsonParser::parseTiles(root, manifest);
    AssetManifestJsonParser::parseSounds(root, manifest);
    AssetManifestJsonParser::parseMusic(root, manifest);
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
    try {
        return parse(buffer.str());
    } catch (const std::exception& error) {
        throw std::runtime_error(file.string() + ": " + error.what());
    }
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
        if (duplicate(textures_, texture.name)) {
            throw std::runtime_error("asset manifest: duplicate texture '" + texture.name + "'");
        }
    }
    if (textures_.size() > maxModelTextures) {
        throw std::runtime_error(
            "asset manifest: too many textures (max " + std::to_string(maxModelTextures) + ")");
    }

    for (Model& model : models_) {
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
        if (tileModelNames_[i].empty()) {
            continue;
        }
        try {
            tileVisuals_[i].model = modelIdByName(tileModelNames_[i]);
        } catch (const std::exception&) {
            throw std::runtime_error(
                "asset manifest: tile '" +
                std::string(tileTypeName(static_cast<TileType>(i))) +
                "' references unknown model '" + tileModelNames_[i] + "'");
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

float AssetManifest::soundSetVolume(std::string_view name) const
{
    for (const SoundSet& set : sounds_) {
        if (set.name == name) {
            return set.volume;
        }
    }
    return 1.0f;
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
