#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/Level.hpp"
#include "engine/ParticleConfig.hpp"
#include "engine/render/RenderTypes.hpp"

#include <algorithm>
#include <array>
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

    const float x = static_cast<float>(gate.cell.x);
    const float y = static_cast<float>(gate.cell.y);
    const float z = static_cast<float>(gate.cell.z);
    const auto appendEnergyBox = [&](
                                     Vec2 position,
                                     Vec2 size,
                                     float baseElevation,
                                     float height,
                                     float alpha) {
        frame.tiles.push_back({
            .cell = gate.cell,
            .position = position,
            .size = size,
            .color = {
                gate.color.x,
                gate.color.y,
                gate.color.z,
                alpha * opacity,
            },
            .baseElevation = baseElevation,
            .height = height,
            .pickable = false,
            .showGrid = false,
            .affectsCameraFit = false,
            .effect = RenderSurfaceEffect::GateEnergy,
        });
    };

    // Two nested surfaces supply the luminous volume. The existing energy
    // shader contributes scan lines, view-angle rim light, and a slow pulse.
    appendEnergyBox(
        { x + 0.035f, y + 0.035f },
        { 0.93f, 0.93f },
        z + 0.035f,
        0.93f,
        config::gateEnergyShellAlpha);
    appendEnergyBox(
        { x + 0.12f, y + 0.12f },
        { 0.76f, 0.76f },
        z + 0.12f,
        0.76f,
        config::gateEnergyCoreAlpha);

    // A bright rail along each of the twelve cube edges gives the field the
    // same strongly framed silhouette as the reference energy barriers.
    constexpr float one = 1.0f;
    const float inset = config::gateEnergyInset;
    const float thickness = config::gateEnergyEdgeThickness;
    const float length = one - inset * 2.0f;
    const float farEdge = one - inset - thickness;
    const float upperEdge = z + one - inset - thickness;
    for (float edgeY : std::array { y + inset, y + farEdge }) {
        appendEnergyBox(
            { x + inset, edgeY },
            { length, thickness },
            z + inset,
            thickness,
            config::gateEnergyEdgeAlpha);
        appendEnergyBox(
            { x + inset, edgeY },
            { length, thickness },
            upperEdge,
            thickness,
            config::gateEnergyEdgeAlpha);
    }
    for (float edgeX : std::array { x + inset, x + farEdge }) {
        appendEnergyBox(
            { edgeX, y + inset },
            { thickness, length },
            z + inset,
            thickness,
            config::gateEnergyEdgeAlpha);
        appendEnergyBox(
            { edgeX, y + inset },
            { thickness, length },
            upperEdge,
            thickness,
            config::gateEnergyEdgeAlpha);
        for (float edgeY : std::array { y + inset, y + farEdge }) {
            appendEnergyBox(
                { edgeX, edgeY },
                { thickness, thickness },
                z + inset,
                length,
                config::gateEnergyEdgeAlpha);
        }
    }

    // The texture is now an accent rather than the body of the gate: one
    // softly pulsing bloom per corner, behind the crisp shader-built rails.
    const RenderTexture texture = manifest.findTextureIdByName(
        config::gateParticleTextureName);
    uint32_t cornerIndex = 0;
    for (float cornerZ : std::array { z + inset, z + one - inset }) {
        for (float cornerY : std::array { y + inset, y + one - inset }) {
            for (float cornerX : std::array { x + inset, x + one - inset }) {
                const float phase = animationTimeSeconds * 2.1f +
                    static_cast<float>(cornerIndex++) * 0.79f;
                const float pulse = 0.88f + 0.12f * std::sin(phase);
                frame.particles.push_back({
                    .position = { cornerX, cornerY, cornerZ },
                    .size = {
                        config::gateCornerParticleSize * pulse,
                        config::gateCornerParticleSize * pulse,
                    },
                    .color = {
                        gate.color.x,
                        gate.color.y,
                        gate.color.z,
                        config::gateCornerParticleOpacity * opacity * pulse,
                    },
                    .emissiveStrength =
                        config::gateCornerParticleEmissiveStrength,
                    .texture = texture,
                    .drawOrder = 1,
                });
            }
        }
    }
}

} // namespace sokoban
