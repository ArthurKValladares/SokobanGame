#pragma once

#include "engine/TileTypes.hpp"
#include "engine/render/RenderTypes.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

struct AssetManifestJsonParser;

// Shader/pipeline cap for the model texture descriptor array. Shaders are
// compiled with MODEL_TEXTURE_COUNT set to this value (see CMakeLists.txt);
// the manifest may declare at most this many textures.
inline constexpr uint32_t maxModelTextures = 16;

enum class ModelGeometry {
    Static,
    Skinned,
};

enum class ModelMaterialMode : uint32_t {
    Untextured = 0,
    SingleTexture = 1,
    PrimitiveTextureIndex = 2,
};

[[nodiscard]] constexpr float shaderValue(ModelMaterialMode mode)
{
    return static_cast<float>(mode);
}

// Runtime asset manifest: the single source of truth for model, texture,
// animation, tile-visual, and sound definitions. Parsed from versioned JSON in
// assets/manifest.json; adding an asset or tile visual requires no CMake, enum,
// or renderer change. Headless and testable in isolation.
class AssetManifest {
public:
    struct Texture {
        std::string name;
        std::string path; // relative to the assets root
    };

    struct Model {
        std::string name;
        std::string path; // relative to the assets root
        ModelGeometry geometry = ModelGeometry::Static;
        bool preserveAspectRatio = false;
        bool rotateHalfTurn = false;
        bool primitiveTextures = false;
        bool beltScroll = false; // UVs scroll with the conveyor clock
        bool playerRole = false; // the model gameplay animates as the player
        ModelMaterialMode materialMode = ModelMaterialMode::Untextured;
        uint32_t textureIndex = 0; // resolved into the texture list
        std::string materialTextureName; // as written in the manifest
    };

    struct Animation {
        std::string name;
        std::string path; // relative to the assets root
        uint32_t clip = 0; // animation index inside the source file
        std::string role; // "", "player-idle", "player-move", "player-push"
    };

    struct TileVisual {
        RenderModel model = cubeModel; // cube renders as a colored box
        float scale = 1.0f;
    };

    struct TileEntry {
        TileType tile = TileType::Ground;
        std::string modelName; // empty selects the procedural cube
        float scale = 1.0f;
    };

    struct SoundSet {
        std::string name;
        std::vector<std::string> files; // relative to the assets root
        float volume = 1.0f; // relative to the master volume
    };

    struct MusicTrack {
        int level = 0;
        std::string file; // relative to the assets root
        float volume = 1.0f; // multiplies the global music volume
    };

    // Throws std::runtime_error with JSON byte/context information on any
    // syntax, schema, or domain-validation failure.
    [[nodiscard]] static AssetManifest parse(std::string_view text);
    [[nodiscard]] static AssetManifest loadFromFile(const std::filesystem::path& file);

    [[nodiscard]] const std::vector<Texture>& textures() const { return textures_; }
    [[nodiscard]] const std::vector<Model>& models() const { return models_; }
    [[nodiscard]] const std::vector<Animation>& animations() const { return animations_; }
    [[nodiscard]] const std::vector<TileEntry>& tileEntries() const { return tiles_; }
    [[nodiscard]] const std::vector<SoundSet>& soundSets() const { return sounds_; }
    [[nodiscard]] const std::vector<MusicTrack>& musicTracks() const { return music_; }

    // Id lookups. Ids address the manifest lists (value-1); see RenderTypes.
    [[nodiscard]] RenderModel modelIdByName(std::string_view name) const; // throws if unknown
    [[nodiscard]] RenderAnimation animationIdByName(std::string_view name) const; // throws if unknown
    [[nodiscard]] const Model& model(RenderModel id) const; // throws for cube/out of range
    [[nodiscard]] const Animation& animation(RenderAnimation id) const; // throws for none/out of range

    [[nodiscard]] const TileVisual& tileVisual(TileType type) const;
    [[nodiscard]] RenderModel modelForTile(TileType type) const { return tileVisual(type).model; }
    [[nodiscard]] float tileScale(TileType type) const { return tileVisual(type).scale; }

    [[nodiscard]] RenderModel playerModel() const { return playerModel_; }
    [[nodiscard]] RenderAnimation playerIdleAnimation() const { return playerIdle_; }
    [[nodiscard]] RenderAnimation playerMoveAnimation() const { return playerMove_; }
    [[nodiscard]] RenderAnimation playerPushAnimation() const { return playerPush_; }

    // Returns an empty list for unknown set names.
    [[nodiscard]] const std::vector<std::string>& soundSet(std::string_view name) const;
    // Returns 1.0 for unknown set names.
    [[nodiscard]] float soundSetVolume(std::string_view name) const;
    // Returns nullptr when the level has no soundtrack.
    [[nodiscard]] const std::string* musicForLevel(int level) const;

private:
    friend struct AssetManifestJsonParser;

    void validateAndResolve();

    std::vector<Texture> textures_;
    std::array<std::string, tileTypeCount> tileModelNames_ {};
    std::vector<Model> models_;
    std::vector<Animation> animations_;
    std::vector<TileEntry> tiles_;
    std::array<TileVisual, tileTypeCount> tileVisuals_ {};
    std::vector<SoundSet> sounds_;
    std::vector<MusicTrack> music_;
    RenderModel playerModel_ {};
    RenderAnimation playerIdle_ {};
    RenderAnimation playerMove_ {};
    RenderAnimation playerPush_ {};
};

} // namespace sokoban
