#pragma once

#include "engine/Math.hpp"
#include "engine/render/RenderTypes.hpp"
#include "engine/render/SceneDrawLanes.hpp"

#include <cstdint>

namespace sokoban {

class AssetManifest;
class PresentationSettings;

// Where the motes live and what they look like. ambientMoteField fills one
// from the tunables in AmbientMotesConfig.hpp; tests build their own.
struct AmbientMoteField {
    // The box of air, in world units.
    Vec3 minimum {};
    Vec3 maximum {};
    // The share of unit cells that hold a mote.
    float density = 0.0f;
    float size = 0.0f;
    Vec3 color { 1.0f, 1.0f, 1.0f };
    float opacity = 0.0f;
    float glow = 1.0f;
    // How much of a mote shows where the cloud light is closed.
    float shadeVisibility = 1.0f;
    // World units per second.
    Vec3 driftVelocity {};
    float wanderRadius = 0.0f;
    float wanderSpeed = 0.0f;
    float edgeFade = 0.0f;
    uint32_t limit = 0;
};

// The field for a gameplay frame: the framed board with the configured reach
// around it, from below the level's floor to above its tallest layer.
[[nodiscard]] AmbientMoteField ambientMoteField(const RenderFrameData& frame);

// Appends the motes of one moment to the frame's particles and returns how
// many it added. Nothing is stored between frames: a mote is a function of
// its cell in a lattice that drifts with the wind and of the time, so the
// same moment always gives the same motes, and the field can move or change
// size without a mote jumping. clouds is the frame's cloud light, which
// decides how much of each mote shows.
uint32_t appendAmbientMotes(
    RenderFrameData& frame,
    const AmbientMoteField& field,
    const CloudLightLanes& clouds,
    RenderTexture texture,
    float timeSeconds);

// All the ambient life of a finished frame that asked for it: the configured
// motes, lit by the frame's own cloud light and timed by its own effect
// clock, if the session's switch is on. Every builder of a frame the player
// watches ends with this, play and editor alike, so that a level looks the
// same however it was opened. Call it last: the motes are the faintest
// thing in the air, and nothing else should be drawn through them.
uint32_t appendAmbientLife(
    RenderFrameData& frame,
    const AssetManifest& manifest,
    const PresentationSettings& settings);

} // namespace sokoban
