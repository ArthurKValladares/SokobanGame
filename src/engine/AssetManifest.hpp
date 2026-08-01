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

// Manifest animation clips use the one-based numbering shown by asset tools.
// Zero remains an alias for the first animation for older/default entries.
[[nodiscard]] constexpr uint32_t animationIndexFromManifestClip(uint32_t clipNumber)
{
    return clipNumber == 0 ? 0 : clipNumber - 1;
}

// Shader/pipeline cap for the model texture descriptor array, and the single
// source of truth for its size: CMakeLists.txt parses this line and compiles
// every shader with MODEL_TEXTURE_COUNT set to it, so the two cannot drift.
// To grow the array, edit this number and re-run CMake configure.
//
// Every screen costs a slot, because each one declares its own ground splat
// map. Exceeding the cap throws while loading the manifest, so a campaign that
// outgrows the array fails loudly at startup rather than rendering wrong, and
// `VulkanDeviceContext` rejects any device whose per-stage sampled-image limit
// cannot hold this many.
inline constexpr uint32_t maxModelTextures = 64;

enum class ModelGeometry {
    Static,
    Skinned,
};

enum class TextureFilter {
    Nearest,
    Linear,
};

enum class TextureColorSpace {
    Srgb,
    Linear,
};

enum class ModelMaterialMode : uint32_t {
    Untextured = 0,
    SingleTexture = 1,
    PrimitiveMaterials = 2,
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
        // Sample with repeat addressing instead of clamp-to-edge. Required
        // by textures whose UVs leave 0..1 (the tiling ground material
        // layers); clamping those smears the edge texel into streaks.
        bool tiling = false;
        // Interpolate between texels instead of point sampling. Wanted by
        // anything magnified across whole tiles; the pixel-art UI and model
        // atlases stay nearest so they keep their crisp texel edges.
        TextureFilter filter = TextureFilter::Nearest;
        // sRGB textures are color and get decoded to linear on read. Data
        // textures (the ground splat weight map) must not be: a painted 50%
        // grey has to arrive at the shader as a 0.5 blend weight, not 0.21.
        TextureColorSpace colorSpace = TextureColorSpace::Srgb;
    };

    struct Model {
        struct Attachment {
            std::string path;
            std::string node;
            bool rotateHalfTurn = false;
        };

        struct PrimitiveMaterial {
            std::string textureName;
            uint32_t textureIndex = 0;
            bool scrollV = false;
        };

        std::string name;
        std::string path; // relative to the assets root
        ModelGeometry geometry = ModelGeometry::Static;
        bool preserveAspectRatio = false;
        // Keep authored mesh units and origin instead of remapping the source
        // bounds into the engine's unit tile. Used by free-form decorations.
        bool preserveSourceScale = false;
        bool rotateHalfTurn = false;
        bool playerRole = false; // the model gameplay animates as the player
        bool enemyRole = false; // the model gameplay animates as an enemy
        ModelMaterialMode materialMode = ModelMaterialMode::Untextured;
        uint32_t textureIndex = 0; // resolved single-texture descriptor index
        std::string materialTextureName; // as written in the manifest
        // Entry N describes glTF material N. Texture names resolve once during
        // validation, so descriptor ordering and material behavior are never
        // inferred from unrelated global indices.
        std::vector<PrimitiveMaterial> primitiveMaterials;
        // Static meshes attached to named skeleton nodes. Attachment geometry
        // uses the owning model's material and is skinned into the same dynamic
        // mesh, so it cannot drift from the sampled pose.
        std::vector<Attachment> attachments;

        [[nodiscard]] bool hasScrollingMaterial() const
        {
            for (const PrimitiveMaterial& material : primitiveMaterials) {
                if (material.scrollV) {
                    return true;
                }
            }
            return false;
        }
    };

    struct Animation {
        std::string name;
        std::string path; // relative to the assets root
        uint32_t clip = 0; // one-based animation number inside the source file
        // "", "player-idle", "player-move", "player-push", "player-death",
        // or "player-dead-idle"
        std::string role;
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

    // Appends a texture to the live manifest, for the level editor creating a
    // splat map for a screen that has none yet. Returns the new id, or
    // `noTexture` when the name is already taken, the name or path is empty,
    // or the descriptor array is full - the same rules parsing enforces.
    //
    // Ids are indices into this list, so appending only ever adds a new id and
    // never disturbs an existing one. Callers must still grow any parallel
    // per-texture state (see VulkanModelResources::syncManifestTextures) and
    // persist the entry, or it is lost on restart.
    [[nodiscard]] RenderTexture addTexture(Texture texture);
    // Appends an ordinary static model for Debug authoring. Existing ids stay
    // stable because model ids, like texture ids, are ordered list indices.
    // Returns cubeModel when validation fails.
    [[nodiscard]] RenderModel addModel(Model model);
    [[nodiscard]] const std::vector<Model>& models() const { return models_; }
    [[nodiscard]] const std::vector<Animation>& animations() const { return animations_; }
    [[nodiscard]] const std::vector<TileEntry>& tileEntries() const { return tiles_; }
    [[nodiscard]] const std::vector<SoundSet>& soundSets() const { return sounds_; }
    [[nodiscard]] const std::vector<MusicTrack>& musicTracks() const { return music_; }

    // Id lookups. Ids address the manifest lists (value-1); see RenderTypes.
    [[nodiscard]] RenderModel modelIdByName(std::string_view name) const; // throws if unknown
    [[nodiscard]] RenderAnimation animationIdByName(std::string_view name) const; // throws if unknown
    [[nodiscard]] RenderTexture textureIdByName(std::string_view name) const; // throws if unknown
    // Optional lookup for textures a feature can render without; returns
    // noTexture when the manifest does not declare `name`.
    [[nodiscard]] RenderTexture findTextureIdByName(std::string_view name) const;
    [[nodiscard]] const Model& model(RenderModel id) const; // throws for cube/out of range
    [[nodiscard]] const Animation& animation(RenderAnimation id) const; // throws for none/out of range

    [[nodiscard]] const TileVisual& tileVisual(TileType type) const;
    [[nodiscard]] RenderModel modelForTile(TileType type) const { return tileVisual(type).model; }
    [[nodiscard]] float tileScale(TileType type) const { return tileVisual(type).scale; }

    [[nodiscard]] RenderModel playerModel() const { return playerModel_; }
    [[nodiscard]] RenderAnimation playerIdleAnimation() const { return playerIdle_; }
    [[nodiscard]] RenderAnimation playerMoveAnimation() const { return playerMove_; }
    [[nodiscard]] RenderAnimation playerPushAnimation() const { return playerPush_; }
    [[nodiscard]] RenderAnimation playerDeathAnimation() const { return playerDeath_; }
    [[nodiscard]] RenderAnimation playerDeadIdleAnimation() const { return playerDeadIdle_; }
    [[nodiscard]] RenderModel enemyModel() const { return enemyModel_; }
    [[nodiscard]] RenderAnimation enemyAttackAnimation() const { return enemyAttack_; }

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
    RenderAnimation playerDeath_ {};
    RenderAnimation playerDeadIdle_ {};
    RenderModel enemyModel_ {};
    RenderAnimation enemyAttack_ {};
};

} // namespace sokoban
