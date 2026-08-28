#pragma once

#include "engine/TileTypes.hpp"
#include "engine/render/RenderTypes.hpp"

#include <array>

namespace sokoban {

class AssetManifest;

// Mutable presentation tuning initialized from focused render configuration.
// Debug UI and future settings screens edit this object; frame
// construction consumes its normalized renderer-facing values.
class PresentationSettings {
public:
    struct Lighting {
        float sunAzimuthDegrees = 0.0f;
        float sunTiltDegrees = 0.0f;
        Vec3 sunColor {};
        float sunIntensity = 0.0f;
        Vec3 ambientColor {};
        float ambientIntensity = 0.0f;
        float specularStrength = 0.0f;
        float modelShadowReceive = 0.0f;
        bool ambientOcclusionEnabled = false;
        float ambientOcclusionStrength = 0.0f;
        RenderFrameData::Lighting::AmbientOcclusion::Debug
            ambientOcclusionDebug =
                RenderFrameData::Lighting::AmbientOcclusion::Debug::Off;
        bool shadowsEnabled = false;
        float shadowOpacity = 0.0f;
        float shadowBias = 0.0f;
    };

    struct Grid {
        Vec4 color {};
        float lineWidth = 0.0f;
    };

    struct Geometry {
        float surfaceEntityHeight = 0.0f;
        float surfaceEntityWidthDepth = 0.0f;
    };

    PresentationSettings();

    Lighting lighting;
    Grid grid;
    Geometry geometry;
    RenderFrameData::OutputTransform outputTransform;
    RenderFrameData::WaterRendering water;

    void normalize();
    // Seeds per-tile render scales from the asset manifest's tile visuals.
    void applyTileScales(const AssetManifest& manifest);
    void setTileScale(TileType type, float scale);
    [[nodiscard]] float tileScale(TileType type) const;
    [[nodiscard]] Vec3 sunDirection() const;
    [[nodiscard]] RenderFrameData::Lighting renderLighting() const;
    [[nodiscard]] RenderFrameData::GridOverlay renderGridOverlay() const;
    [[nodiscard]] RenderFrameData::OutputTransform renderOutputTransform() const;

private:
    // Defaults come from the asset manifest via applyTileScales at startup.
    std::array<float, tileTypeCount> tileScales_;
};

} // namespace sokoban
