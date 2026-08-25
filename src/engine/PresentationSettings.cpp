#include "engine/PresentationSettings.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/render/LightingConfig.hpp"
#include "engine/render/SceneConfig.hpp"
#include "engine/render/WaterConfig.hpp"

#include <algorithm>
#include <cmath>

namespace sokoban {
namespace {

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

    water.surfaceColor.x = std::clamp(water.surfaceColor.x, 0.0f, 1.0f);
    water.surfaceColor.y = std::clamp(water.surfaceColor.y, 0.0f, 1.0f);
    water.surfaceColor.z = std::clamp(water.surfaceColor.z, 0.0f, 1.0f);
    water.surfaceColor.w = std::clamp(water.surfaceColor.w, 0.0f, 0.95f);
    water.primaryRippleOpacity =
        std::clamp(water.primaryRippleOpacity, 0.0f, 1.0f);
    water.secondaryRippleOpacity =
        std::clamp(water.secondaryRippleOpacity, 0.0f, 1.0f);
    water.rippleSpatialFrequency = std::clamp(
        water.rippleSpatialFrequency,
        config::minimumWaterRippleSpatialFrequency,
        config::maximumWaterRippleSpatialFrequency);
    water.rippleSpeed = std::clamp(
        water.rippleSpeed,
        config::minimumWaterRippleSpeed,
        config::maximumWaterRippleSpeed);
    water.refractionStrength = std::clamp(
        water.refractionStrength,
        config::minimumWaterRefractionStrength,
        config::maximumWaterRefractionStrength);
    water.rippleCrestHalfWidth = std::clamp(
        water.rippleCrestHalfWidth,
        config::minimumWaterRippleWidth,
        config::maximumWaterRippleWidth);
    water.rippleHaloWidth = std::clamp(
        water.rippleHaloWidth,
        water.rippleCrestHalfWidth,
        config::maximumWaterRippleWidth);
    water.rippleHaloStrength =
        std::clamp(water.rippleHaloStrength, 0.0f, 1.0f);
    water.rippleCrestStrength =
        std::clamp(water.rippleCrestStrength, 0.0f, 1.0f);
    water.secondaryRippleThicknessScale = std::clamp(
        water.secondaryRippleThicknessScale,
        config::minimumWaterSecondaryRippleThicknessScale,
        config::maximumWaterSecondaryRippleThicknessScale);
    water.underwaterCausticStrength =
        std::clamp(water.underwaterCausticStrength, 0.0f, 1.0f);
    water.primaryShorelineOpacity =
        std::clamp(water.primaryShorelineOpacity, 0.0f, 1.0f);
    water.secondaryShorelineOpacity =
        std::clamp(water.secondaryShorelineOpacity, 0.0f, 1.0f);

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
