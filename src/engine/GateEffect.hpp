#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/Level.hpp"
#include "engine/ParticleConfig.hpp"
#include "engine/render/RenderTypes.hpp"

#include <algorithm>
#include <cmath>

namespace sokoban {

inline void appendGateEffect(
    RenderFrameData& frame,
    const Level::Gate& gate,
    const AssetManifest& manifest,
    float opacity,
    float animationTimeSeconds)
{
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    if (opacity <= 0.001f) {
        return;
    }
    const RenderTexture texture = manifest.findTextureIdByName(
        config::gateParticleTextureName);
    for (uint32_t row = 0; row < config::gateParticleRows; ++row) {
        for (uint32_t column = 0;
             column < config::gateParticleColumns;
             ++column) {
            const uint32_t index = row * config::gateParticleColumns + column;
            const float phase = animationTimeSeconds * 1.8f +
                static_cast<float>(index) * 1.71f;
            const float x = static_cast<float>(gate.cell.x) + 0.22f +
                static_cast<float>(column) * 0.28f +
                std::sin(phase) * config::gateParticleDrift;
            const float y = static_cast<float>(gate.cell.y) + 0.5f +
                std::cos(phase * 0.83f) * config::gateParticleDrift;
            const float z = static_cast<float>(gate.cell.z) + 0.16f +
                static_cast<float>(row) * 0.24f +
                std::sin(phase * 0.61f) * 0.035f;
            const float pulse = 0.82f + 0.18f * std::sin(phase * 1.17f);
            frame.particles.push_back({
                .position = { x, y, z },
                .size = {
                    config::gateParticleSize * pulse,
                    config::gateParticleSize * pulse,
                },
                .color = {
                    gate.color.x,
                    gate.color.y,
                    gate.color.z,
                    config::gateParticleOpacity * opacity *
                        (0.74f + 0.26f * pulse),
                },
                .emissiveStrength = config::gateParticleEmissiveStrength,
                .texture = texture,
            });
        }
    }
}

} // namespace sokoban
