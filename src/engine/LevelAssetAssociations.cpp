#include "engine/LevelAssetAssociations.hpp"

#include "engine/AssetManifestEditor.hpp"
#include "engine/render/RenderTypes.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string_view>

namespace sokoban {
namespace {

using Json = nlohmann::ordered_json;

std::optional<LevelLocation> splatLocation(std::string_view name)
{
    if (!name.starts_with(groundSplatMapTextureName) ||
        name.size() == groundSplatMapTextureName.size()) {
        return std::nullopt;
    }
    name.remove_prefix(groundSplatMapTextureName.size());
    const std::size_t separator = name.find('_');
    if (separator == std::string_view::npos) {
        return std::nullopt;
    }
    LevelLocation result;
    const auto levelResult = std::from_chars(
        name.data(), name.data() + separator, result.level);
    const auto screenResult = std::from_chars(
        name.data() + separator + 1, name.data() + name.size(), result.screen);
    if (levelResult.ec != std::errc {} ||
        levelResult.ptr != name.data() + separator ||
        screenResult.ec != std::errc {} ||
        screenResult.ptr != name.data() + name.size()) {
        return std::nullopt;
    }
    return result;
}

void saveEditor(AssetManifestEditor& editor)
{
    if (!editor.save()) {
        throw std::runtime_error(editor.status());
    }
}

AssetManifestEditor loadEditor(const std::filesystem::path& path)
{
    AssetManifestEditor editor;
    if (!editor.load(path)) {
        throw std::runtime_error(editor.status());
    }
    return editor;
}

Json textureJson(const AssetManifest::Texture& texture)
{
    return {
        { "name", texture.name },
        { "path", texture.path },
        { "tiling", texture.tiling },
        { "filter", texture.filter == TextureFilter::Linear
            ? "linear" : "nearest" },
        { "colorSpace", texture.colorSpace == TextureColorSpace::Linear
            ? "linear" : "srgb" },
    };
}

AssetManifest::Texture textureFromJson(const Json& value)
{
    return {
        .name = value.at("name").get<std::string>(),
        .path = value.at("path").get<std::string>(),
        .tiling = value.value("tiling", false),
        .filter = value.value("filter", std::string("nearest")) == "linear"
            ? TextureFilter::Linear : TextureFilter::Nearest,
        .colorSpace = value.value("colorSpace", std::string("srgb")) ==
                "linear"
            ? TextureColorSpace::Linear : TextureColorSpace::Srgb,
    };
}

} // namespace

void remapLevelAssetAssociations(
    const std::filesystem::path& manifestPath,
    std::span<const LevelLocationAssociationRemap> remaps)
{
    AssetManifestEditor editor = loadEditor(manifestPath);

    std::vector<std::size_t> removedTextures;
    for (std::size_t index = 0; index < editor.textures().size(); ++index) {
        AssetManifest::Texture texture = editor.textures()[index];
        const std::optional<LevelLocation> location =
            splatLocation(texture.name);
        if (!location) {
            continue;
        }
        const auto remap = std::ranges::find(remaps, *location,
            &LevelLocationAssociationRemap::source);
        if (remap == remaps.end()) {
            continue;
        }
        if (remap->destination) {
            texture.name = groundSplatMapTextureNameForScreen(
                *remap->destination);
            editor.updateTexture(index, std::move(texture));
        } else {
            removedTextures.push_back(index);
        }
    }
    for (auto index = removedTextures.rbegin();
         index != removedTextures.rend(); ++index) {
        (void)editor.removeTexture(*index);
    }

    std::map<int, std::optional<int>> levelDestinations;
    for (const LevelLocationAssociationRemap& remap : remaps) {
        auto& destination = levelDestinations[remap.source.level];
        if (remap.destination) {
            if (destination && *destination != remap.destination->level) {
                throw std::runtime_error(
                    "one level cannot be remapped to multiple level indices");
            }
            destination = remap.destination->level;
        }
    }
    std::vector<std::size_t> removedMusic;
    for (std::size_t index = 0; index < editor.musicTracks().size(); ++index) {
        AssetManifest::MusicTrack track = editor.musicTracks()[index];
        const auto destination = levelDestinations.find(track.level);
        if (destination == levelDestinations.end()) {
            continue;
        }
        if (destination->second) {
            track.level = *destination->second;
            editor.updateMusicTrack(index, std::move(track));
        } else {
            removedMusic.push_back(index);
        }
    }
    for (auto index = removedMusic.rbegin(); index != removedMusic.rend(); ++index) {
        (void)editor.removeMusicTrack(*index);
    }
    saveEditor(editor);
}

DeletedLevelAssetAssociations captureLevelAssetAssociations(
    const std::filesystem::path& manifestPath,
    int level)
{
    const AssetManifest manifest = AssetManifest::loadFromFile(manifestPath);
    DeletedLevelAssetAssociations result { .originalLevel = level };
    for (const AssetManifest::Texture& texture : manifest.textures()) {
        const std::optional<LevelLocation> location = splatLocation(texture.name);
        if (location && location->level == level) {
            result.splatMaps.push_back(texture);
        }
    }
    for (const AssetManifest::MusicTrack& track : manifest.musicTracks()) {
        if (track.level == level) {
            result.music = track;
            break;
        }
    }
    return result;
}

void writeDeletedLevelAssetAssociations(
    const std::filesystem::path& levelRoot,
    const DeletedLevelAssetAssociations& associations)
{
    Json root {
        { "format", 1 },
        { "originalLevel", associations.originalLevel },
        { "splatMaps", Json::array() },
    };
    for (const AssetManifest::Texture& texture : associations.splatMaps) {
        root["splatMaps"].push_back(textureJson(texture));
    }
    if (associations.music) {
        root["music"] = {
            { "file", associations.music->file },
            { "volume", associations.music->volume },
        };
    }
    std::ofstream stream(
        levelRoot / deletedLevelAssetAssociationsFilename,
        std::ios::binary | std::ios::trunc);
    stream << root.dump(2) << '\n';
    if (!stream) {
        throw std::runtime_error(
            "cannot archive deleted level asset associations");
    }
}

std::optional<DeletedLevelAssetAssociations>
readDeletedLevelAssetAssociations(const std::filesystem::path& levelRoot)
{
    const std::filesystem::path path =
        levelRoot / deletedLevelAssetAssociationsFilename;
    if (!std::filesystem::is_regular_file(path)) {
        return std::nullopt;
    }
    std::ifstream stream(path, std::ios::binary);
    const Json root = Json::parse(stream);
    if (root.at("format").get<int>() != 1) {
        throw std::runtime_error(
            "unsupported deleted level asset association format");
    }
    DeletedLevelAssetAssociations result {
        .originalLevel = root.at("originalLevel").get<int>(),
    };
    for (const Json& texture : root.at("splatMaps")) {
        result.splatMaps.push_back(textureFromJson(texture));
    }
    if (root.contains("music")) {
        result.music = AssetManifest::MusicTrack {
            .level = result.originalLevel,
            .file = root.at("music").at("file").get<std::string>(),
            .volume = root.at("music").value("volume", 1.0f),
        };
    }
    return result;
}

void removeDeletedLevelAssetAssociations(
    const std::filesystem::path& levelRoot)
{
    std::error_code error;
    (void)std::filesystem::remove(
        levelRoot / deletedLevelAssetAssociationsFilename, error);
    if (error) {
        throw std::runtime_error(
            "cannot remove restored asset association archive: " +
            error.message());
    }
}

void restoreLevelAssetAssociations(
    const std::filesystem::path& manifestPath,
    const DeletedLevelAssetAssociations& associations,
    int restoredLevel)
{
    AssetManifestEditor editor = loadEditor(manifestPath);
    for (AssetManifest::Texture texture : associations.splatMaps) {
        const std::optional<LevelLocation> oldLocation =
            splatLocation(texture.name);
        if (!oldLocation) {
            continue;
        }
        texture.name = groundSplatMapTextureNameForScreen({
            .level = restoredLevel,
            .screen = oldLocation->screen,
        });
        editor.addTexture();
        editor.updateTexture(editor.textures().size() - 1, std::move(texture));
    }
    if (associations.music) {
        AssetManifest::MusicTrack track = *associations.music;
        track.level = restoredLevel;
        editor.addMusicTrack();
        editor.updateMusicTrack(editor.musicTracks().size() - 1, std::move(track));
    }
    saveEditor(editor);
}

} // namespace sokoban
