#include "engine/PresentationSettings.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/render/LightingConfig.hpp"
#include "engine/render/SceneConfig.hpp"

#include <algorithm>
#include <cmath>

namespace sokoban {
namespace {

float degreesToRadians(float degrees)
{
    constexpr float pi = 3.14159265358979323846f;
    return degrees * pi / 180.0f;
}

float clampedTileScale(float scale)
{
    return std::clamp(scale, config::minTileScale, config::maxTileScale);
}

} // namespace

PresentationSettings::PresentationSettings()
    : lighting {
          .sunAzimuthDegrees = config::sunAzimuthDegrees,
          .sunTiltDegrees = config::sunTiltDegrees,
          .sunColor = config::sunColor,
          .sunIntensity = config::sunIntensity,
          .ambientColor = config::ambientLightColor,
          .ambientIntensity = config::ambientLightIntensity,
          .specularStrength = config::specularStrength,
          .specularPower = config::specularPower,
          .modelShadowReceive = config::modelShadowReceive,
          .ambientOcclusionEnabled = config::ambientOcclusionEnabled,
          .ambientOcclusionStrength = config::ambientOcclusionStrength,
          .ambientOcclusionVisualize = false,
          .shadowsEnabled = config::shadowsEnabled,
          .shadowOpacity = config::shadowOpacity,
          .shadowBias = config::shadowBias,
      }
    , grid {
          .color = config::tileGridLineColor,
          .lineWidth = config::tileGridLineWidth,
      }
    , geometry {
          .surfaceEntityHeight = config::surfaceEntityHeight,
          .surfaceEntityWidthDepth = config::surfaceEntityWidthDepth,
      }
{
    tileScales_.fill(1.0f);
}

void PresentationSettings::applyTileScales(const AssetManifest& manifest)
{
    for (std::size_t i = 0; i < tileScales_.size(); ++i) {
        setTileScale(
            static_cast<TileType>(i),
            manifest.tileScale(static_cast<TileType>(i)));
    }
}

void PresentationSettings::normalize()
{
    lighting.sunAzimuthDegrees = std::clamp(
        lighting.sunAzimuthDegrees,
        config::minimumSunAzimuthDegrees,
        config::maximumSunAzimuthDegrees);
    lighting.sunTiltDegrees = std::clamp(
        lighting.sunTiltDegrees,
        config::minimumSunTiltDegrees,
        config::maximumSunTiltDegrees);
    lighting.sunIntensity = std::clamp(
        lighting.sunIntensity, 0.0f, config::maximumSunIntensity);
    lighting.ambientIntensity = std::clamp(
        lighting.ambientIntensity,
        0.0f,
        config::maximumAmbientLightIntensity);
    lighting.specularStrength = std::clamp(
        lighting.specularStrength, 0.0f, config::maximumSpecularStrength);
    lighting.specularPower = std::clamp(
        lighting.specularPower,
        config::minimumSpecularPower,
        config::maximumSpecularPower);
    lighting.modelShadowReceive = std::clamp(
        lighting.modelShadowReceive,
        0.0f,
        config::maximumModelShadowReceive);
    lighting.ambientOcclusionStrength =
        std::clamp(
            lighting.ambientOcclusionStrength,
            0.0f,
            config::maximumAmbientOcclusionStrength);
    lighting.shadowOpacity = std::clamp(
        lighting.shadowOpacity, 0.0f, config::maximumShadowOpacity);
    lighting.shadowBias = std::clamp(
        lighting.shadowBias, 0.0f, config::maximumShadowBias);

    grid.color.w = std::clamp(grid.color.w, 0.0f, 1.0f);
    grid.lineWidth = std::clamp(
        grid.lineWidth,
        config::minimumTileGridLineWidth,
        config::maximumTileGridLineWidth);
    geometry.surfaceEntityHeight =
        std::clamp(
            geometry.surfaceEntityHeight,
            config::minimumSurfaceEntityHeight,
            config::maximumSurfaceEntityHeight);
    geometry.surfaceEntityWidthDepth =
        std::clamp(
            geometry.surfaceEntityWidthDepth,
            config::minimumSurfaceEntityWidthDepth,
            config::maximumSurfaceEntityWidthDepth);

    for (float& scale : tileScales_) {
        scale = clampedTileScale(scale);
    }
}

void PresentationSettings::setTileScale(TileType type, float scale)
{
    const auto index = static_cast<std::size_t>(type);
    if (index < tileScales_.size()) {
        tileScales_[index] = clampedTileScale(scale);
    }
}

float PresentationSettings::tileScale(TileType type) const
{
    const auto index = static_cast<std::size_t>(type);
    return index < tileScales_.size() ? clampedTileScale(tileScales_[index]) : 1.0f;
}

Vec3 PresentationSettings::sunDirection() const
{
    const float azimuth = degreesToRadians(lighting.sunAzimuthDegrees);
    const float tilt = degreesToRadians(lighting.sunTiltDegrees);
    const float horizontalLength = std::sin(tilt);
    return {
        horizontalLength * std::cos(azimuth),
        horizontalLength * std::sin(azimuth),
        std::cos(tilt),
    };
}

RenderFrameData::Lighting PresentationSettings::renderLighting() const
{
    return {
        .sun = {
            .direction = sunDirection(),
            .color = lighting.sunColor,
            .intensity = lighting.sunIntensity,
        },
        .ambient = {
            .color = lighting.ambientColor,
            .intensity = lighting.ambientIntensity,
        },
        .shadows = {
            .enabled = lighting.shadowsEnabled,
            .opacity = lighting.shadowOpacity,
            .bias = lighting.shadowBias,
        },
        .ambientOcclusion = {
            .enabled = lighting.ambientOcclusionEnabled,
            .strength = lighting.ambientOcclusionStrength,
            .visualize = lighting.ambientOcclusionVisualize,
        },
        .specularStrength = lighting.specularStrength,
        .specularPower = lighting.specularPower,
        .modelShadowReceive = lighting.modelShadowReceive,
    };
}

RenderFrameData::GridOverlay PresentationSettings::renderGridOverlay() const
{
    return {
        .color = grid.color,
        .width = grid.lineWidth,
    };
}

} // namespace sokoban
