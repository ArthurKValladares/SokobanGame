#pragma once

#include "engine/ParticleSystem.hpp"

namespace sokoban {

class AssetManifest;

[[nodiscard]] ParticleEffectDefinition makeMirrorSwapParticleEffect(
    const AssetManifest& manifest);

} // namespace sokoban
