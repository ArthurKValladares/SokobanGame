#pragma once

#include "engine/Config.hpp"
#include "engine/Math.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace sokoban {

enum class RenderViewMode {
    TopDown2D,
    Isometric3D,
};

enum class RenderModel {
    Cube,
    BricksA,
    Stone,
    Water,
    Glass,
    Conveyor,
    Rogue,
};

enum class RenderAnimation {
    None,
    RogueIdle,
    RogueMovement,
    RoguePush,
};

struct RenderFrameData {
    struct DirectionalLight {
        Vec3 direction { 0.0f, 0.0f, 1.0f };
        Vec3 color { config::sunColor };
        float intensity = config::sunIntensity;
    };

    struct AmbientLight {
        Vec3 color { config::ambientLightColor };
        float intensity = config::ambientLightIntensity;
    };

    struct Lighting {
        struct Shadows {
            bool enabled = config::shadowsEnabled;
            float opacity = config::shadowOpacity;
            float bias = config::shadowBias;
        };

        struct AmbientOcclusion {
            bool enabled = config::ambientOcclusionEnabled;
            float strength = config::ambientOcclusionStrength;
            bool visualize = false;
        };

        DirectionalLight sun {};
        AmbientLight ambient {};
        Shadows shadows {};
        AmbientOcclusion ambientOcclusion {};
        float specularStrength = config::specularStrength;
        float specularPower = config::specularPower;
        float modelShadowReceive = config::modelShadowReceive;
    };

    struct Tile {
        GridPosition3 cell {};
        Vec2 position {};
        Vec2 size { 1.0f, 1.0f };
        Vec4 color {};
        float baseElevation = 0.0f;
        float height = 0.0f;
        bool blurBehind = false;
        bool pickOnly = false;
        bool showGrid = true;
        bool isEditorPreview = false;
        RenderModel model = RenderModel::Cube;
        RenderAnimation animation = RenderAnimation::None;
        float animationTimeSeconds = 0.0f;
        float beltScrollOffset = 0.0f;
        uint32_t modelRotationQuarterTurns = 0;
    };

    struct IsoFace {
        std::array<Vec3, 4> vertices {};
        Vec3 normal {};
        Vec4 color {};
    };

    struct GridOverlay {
        Vec4 color { config::tileGridLineColor };
        float width = config::tileGridLineWidth;
    };

    RenderViewMode viewMode = RenderViewMode::TopDown2D;
    Lighting lighting {};
    GridOverlay gridOverlay {};
    uint32_t levelWidth = 0;
    uint32_t levelHeight = 0;
    uint32_t levelDepth = 1;
    Vec2 playerPosition {};
    std::vector<Tile> tiles;
    std::vector<IsoFace> isoFaces;
};

struct RenderStats {
    uint64_t frameIndex = 0;
    uint32_t totalTiles = 0;
    uint32_t visibleFaces = 0;
    uint32_t drawCalls = 0;
    uint32_t vertices = 0;
    uint32_t triangles = 0;
    uint32_t pipelineBinds = 0;
    uint32_t renderPasses = 0;
    uint32_t imageBarriers = 0;
    uint32_t swapchainWidth = 0;
    uint32_t swapchainHeight = 0;
    uint32_t swapchainImages = 0;
    uint32_t activeSamples = 1;
    bool wireframeEnabled = false;
    float wireframeLineWidth = 1.0f;
    uint64_t pipelineRebuilds = 0;
    uint64_t swapchainRecreations = 0;
};

} // namespace sokoban
