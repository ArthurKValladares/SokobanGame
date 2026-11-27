#include "engine/PresentationSettings.hpp"

#include "engine/AmbientMotesConfig.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/render/CloudLightConfig.hpp"
#include "engine/render/IslandMistConfig.hpp"
#include "engine/render/LightingConfig.hpp"
#include "engine/render/SceneConfig.hpp"
#include "engine/render/WaterConfig.hpp"
#include "engine/render/WindConfig.hpp"

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
          .modelShadowReceive = config::modelShadowReceive,
          .ambientOcclusionEnabled = config::ambientOcclusionEnabled,
          .ambientOcclusionStrength = config::ambientOcclusionStrength,
          .ambientOcclusionDebug =
              RenderFrameData::Lighting::AmbientOcclusion::Debug::Off,
          .atmosphereEnabled = config::atmosphereEnabled,
          .atmosphereColor = config::atmosphereColor,
          .atmosphereDensity = config::atmosphereDensity,
          .atmosphereHeightFalloff = config::atmosphereHeightFalloff,
          .atmosphereBaseHeight = config::atmosphereBaseHeight,
          .atmosphereMaxDistance = config::atmosphereMaxDistance,
          .atmosphereScatteringStrength =
              config::atmosphereScatteringStrength,
          .atmosphereAnisotropy = config::atmosphereAnisotropy,
          .islandMistEnabled = config::islandMistEnabled,
          .cloudLightEnabled = config::cloudLightEnabled,
          .ambientMotesEnabled = config::ambientMotesEnabled,
          .windEnabled = config::windEnabled,
          .shadowsEnabled = config::shadowsEnabled,
          .shadowOpacity = config::shadowOpacity,
          .shadowBias = config::shadowBias,
          .stylization = {
              .diffuseSoftness = config::diffuseSoftness,
              .normalDetail = config::normalDetail,
              .roughnessFloor = config::roughnessFloor,
          },
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
    geometry.groundRimWidth = std::isfinite(geometry.groundRimWidth)
        ? std::clamp(geometry.groundRimWidth, 0.02f, 0.25f) : 0.12f;
    geometry.groundRimDepth = std::isfinite(geometry.groundRimDepth)
        ? std::clamp(geometry.groundRimDepth, 0.01f, 0.20f) : 0.10f;
    outputTransform.exposureEv = normalizedExposureEv(
        outputTransform.exposureEv);
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
    lighting.modelShadowReceive = std::clamp(
        lighting.modelShadowReceive,
        0.0f,
        config::maximumModelShadowReceive);
    lighting.ambientOcclusionStrength =
        std::clamp(
            lighting.ambientOcclusionStrength,
            0.0f,
            config::maximumAmbientOcclusionStrength);
    lighting.atmosphereColor.x = std::max(lighting.atmosphereColor.x, 0.0f);
    lighting.atmosphereColor.y = std::max(lighting.atmosphereColor.y, 0.0f);
    lighting.atmosphereColor.z = std::max(lighting.atmosphereColor.z, 0.0f);
    lighting.atmosphereDensity = std::clamp(
        lighting.atmosphereDensity, 0.0f,
        config::maximumAtmosphereDensity);
    lighting.atmosphereHeightFalloff = std::clamp(
        lighting.atmosphereHeightFalloff, 0.0f,
        config::maximumAtmosphereHeightFalloff);
    lighting.atmosphereBaseHeight = std::clamp(
        lighting.atmosphereBaseHeight,
        config::minimumAtmosphereBaseHeight,
        config::maximumAtmosphereBaseHeight);
    lighting.atmosphereMaxDistance = std::clamp(
        lighting.atmosphereMaxDistance,
        config::minimumAtmosphereMaxDistance,
        config::maximumAtmosphereMaxDistance);
    lighting.atmosphereScatteringStrength = std::clamp(
        lighting.atmosphereScatteringStrength, 0.0f,
        config::maximumAtmosphereScatteringStrength);
    lighting.atmosphereAnisotropy = std::clamp(
        lighting.atmosphereAnisotropy,
        config::minimumAtmosphereAnisotropy,
        config::maximumAtmosphereAnisotropy);
    lighting.shadowOpacity = std::clamp(
        lighting.shadowOpacity, 0.0f, config::maximumShadowOpacity);
    lighting.shadowBias = std::clamp(
        lighting.shadowBias, 0.0f, config::maximumShadowBias);
    auto& stylization = lighting.stylization;
    stylization.diffuseSoftness = std::isfinite(stylization.diffuseSoftness)
        ? std::clamp(stylization.diffuseSoftness, 0.0f, 1.0f) : 0.0f;
    stylization.normalDetail = std::isfinite(stylization.normalDetail)
        ? std::clamp(stylization.normalDetail, 0.0f, 1.0f) : 1.0f;
    stylization.roughnessFloor = std::isfinite(stylization.roughnessFloor)
        ? std::clamp(stylization.roughnessFloor, 0.0f, 1.0f) : 0.0f;

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
    water.reflectionStrength = std::clamp(
        water.reflectionStrength,
        config::minimumWaterReflectionStrength,
        config::maximumWaterReflectionStrength);
    water.reflectionMaxDistance = std::clamp(
        water.reflectionMaxDistance,
        config::minimumWaterReflectionMaxDistance,
        config::maximumWaterReflectionMaxDistance);
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
            .debug = lighting.ambientOcclusionDebug,
        },
        .atmosphere = {
            .enabled = lighting.atmosphereEnabled,
            .color = lighting.atmosphereColor,
            .density = lighting.atmosphereDensity,
            .heightFalloff = lighting.atmosphereHeightFalloff,
            .baseHeight = lighting.atmosphereBaseHeight,
            .maxDistance = lighting.atmosphereMaxDistance,
            .scatteringStrength = lighting.atmosphereScatteringStrength,
            .anisotropy = lighting.atmosphereAnisotropy,
        },
        // Read on every call rather than cached: these are live tunables in
        // a developer build, and the renderer wants the frame to carry
        // values it can use without checking them again.
        .islandMist = {
            .enabled = lighting.islandMistEnabled,
            .color = {
                std::max(config::islandMistColor.x, 0.0f),
                std::max(config::islandMistColor.y, 0.0f),
                std::max(config::islandMistColor.z, 0.0f),
            },
            .density = std::max(config::islandMistDensity, 0.0f),
            .scatteringStrength =
                std::max(config::islandMistScatteringStrength, 0.0f),
            .anisotropy = std::clamp(
                config::islandMistAnisotropy,
                config::minimumAtmosphereAnisotropy,
                config::maximumAtmosphereAnisotropy),
            .ambientStrength =
                std::max(config::islandMistAmbientStrength, 0.0f),
            .topHeight = config::islandMistTopHeight,
            .depth = std::max(config::islandMistDepth, 0.0f),
            .depthFalloff = std::max(config::islandMistDepthFalloff, 0.0f),
            .reach = std::max(config::islandMistReach, 0.0f),
            .noiseScale = std::max(config::islandMistNoiseScale, 0.0f),
            .noiseStrength = std::max(config::islandMistNoiseStrength, 0.0f),
            .noiseSpeed = config::islandMistNoiseSpeed,
            .colorNoiseScale =
                std::max(config::islandMistColorNoiseScale, 0.0f),
            .colorNoiseStrength =
                std::max(config::islandMistColorNoiseStrength, 0.0f),
            .colorNoiseSpeed = config::islandMistColorNoiseSpeed,
            .sampleCount = config::islandMistSampleCount,
        },
        .cloudLight = {
            .enabled = lighting.cloudLightEnabled,
            .surfaceStrength = std::clamp(
                config::cloudLightSurfaceStrength, 0.0f, 1.0f),
            .mediumStrength = std::clamp(
                config::cloudLightMediumStrength, 0.0f, 1.0f),
            .mediumGain = std::max(config::cloudLightMediumGain, 1.0f),
            .scale = std::max(config::cloudLightScale, 0.0f),
            .coverage = std::clamp(config::cloudLightCoverage, 0.0f, 1.0f),
            .softness = std::max(config::cloudLightSoftness, 0.0f),
            .detailScale = std::max(config::cloudLightDetailScale, 0.0f),
            .detailStrength = std::clamp(
                config::cloudLightDetailStrength, 0.0f, 1.0f),
            .detailStretch = std::clamp(
                config::cloudLightDetailStretch, 0.05f, 1.0f),
            .driftVelocity = {
                config::cloudLightDriftSpeed * std::cos(degreesToRadians(
                    config::cloudLightDriftDirectionDegrees)),
                config::cloudLightDriftSpeed * std::sin(degreesToRadians(
                    config::cloudLightDriftDirectionDegrees)),
            },
        },
        // The grass leans the way the clouds drift: one wind for all of it.
        .wind = {
            .enabled = lighting.windEnabled,
            .direction = {
                std::cos(degreesToRadians(config::cloudLightDriftDirectionDegrees)),
                std::sin(degreesToRadians(config::cloudLightDriftDirectionDegrees)),
            },
            .swayAmplitude = std::max(config::windSwayAmplitude, 0.0f),
            .gustLength = std::max(config::windGustLength, 0.01f),
            .gustSpeed = std::max(config::windGustSpeed, 0.0f),
            .flutter = std::clamp(config::windFlutter, 0.0f, 1.0f),
        },
        .specularStrength = lighting.specularStrength,
        .modelShadowReceive = lighting.modelShadowReceive,
        .stylization = lighting.stylization,
    };
}

RenderFrameData::GridOverlay PresentationSettings::renderGridOverlay() const
{
    return {
        .color = grid.color,
        .width = grid.lineWidth,
    };
}

RenderFrameData::OutputTransform
PresentationSettings::renderOutputTransform() const
{
    return {
        .exposureEv = normalizedExposureEv(outputTransform.exposureEv),
        .curve = outputTransform.curve,
        .bloomWideEnabled = true,
    };
}

} // namespace sokoban
