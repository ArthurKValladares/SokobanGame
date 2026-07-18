#pragma once

#include "engine/Config.hpp"
#include "engine/TileTypes.hpp"
#include "engine/render/RenderTypes.hpp"

#include <array>

namespace sokoban {

class AssetManifest;

// Mutable presentation tuning initialized from the compile-time defaults in
// Config.hpp. Debug UI and future settings screens edit this object; frame
// construction consumes its normalized renderer-facing values.
class PresentationSettings {
public:
    struct Lighting {
        float sunAzimuthDegrees = config::sunAzimuthDegrees;
        float sunTiltDegrees = config::sunTiltDegrees;
        Vec3 sunColor { config::sunColor };
        float sunIntensity = config::sunIntensity;
        Vec3 ambientColor { config::ambientLightColor };
        float ambientIntensity = config::ambientLightIntensity;
        float specularStrength = config::specularStrength;
        float specularPower = config::specularPower;
        float modelShadowReceive = config::modelShadowReceive;
        bool ambientOcclusionEnabled = config::ambientOcclusionEnabled;
        float ambientOcclusionStrength = config::ambientOcclusionStrength;
        bool ambientOcclusionVisualize = false;
        bool shadowsEnabled = config::shadowsEnabled;
        float shadowOpacity = config::shadowOpacity;
        float shadowBias = config::shadowBias;
    };

    struct Grid {
        Vec4 color { config::tileGridLineColor };
        float lineWidth = config::tileGridLineWidth;
    };

    struct Geometry {
        float surfaceEntityHeight = config::surfaceEntityHeight;
        float surfaceEntityWidthDepth = config::surfaceEntityWidthDepth;
    };

    Lighting lighting;
    Grid grid;
    Geometry geometry;

    void normalize();
    // Seeds per-tile render scales from the asset manifest's tile visuals.
    void applyTileScales(const AssetManifest& manifest);
    void setTileScale(TileType type, float scale);
    [[nodiscard]] float tileScale(TileType type) const;
    [[nodiscard]] Vec3 sunDirection() const;
    [[nodiscard]] RenderFrameData::Lighting renderLighting() const;
    [[nodiscard]] RenderFrameData::GridOverlay renderGridOverlay() const;

private:
    // Defaults come from the asset manifest via applyTileScales at startup.
    std::array<float, tileTypeCount> tileScales_ {
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    };
};

} // namespace sokoban
