#include "engine/MirrorParticleEffect.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/ParticleConfig.hpp"
#include "engine/render/MirrorConfig.hpp"

namespace sokoban {

ParticleEffectDefinition makeMirrorSwapParticleEffect(
    const AssetManifest& manifest)
{
    ParticleEffectDefinition effect {
        .color = {
            config::mirrorBeamCoreColor.x,
            config::mirrorBeamCoreColor.y,
            config::mirrorBeamCoreColor.z,
            config::mirrorSwapSmokeOpacity,
        },
        .particleCount = config::mirrorSwapSmokeParticleCount,
        .lifetimeSeconds = config::mirrorSwapSmokeLifetimeSeconds,
        .initialSize = {
            config::mirrorSwapSmokeInitialSize.x *
                config::mirrorSwapSmokeScale,
            config::mirrorSwapSmokeInitialSize.y *
                config::mirrorSwapSmokeScale,
        },
        .finalSize = {
            config::mirrorSwapSmokeFinalSize.x *
                config::mirrorSwapSmokeScale,
            config::mirrorSwapSmokeFinalSize.y *
                config::mirrorSwapSmokeScale,
        },
        .spawnRadius = config::mirrorSwapSmokeSpawnRadius,
        .minimumVelocity = config::mirrorSwapSmokeMinimumVelocity,
        .maximumVelocity = config::mirrorSwapSmokeMaximumVelocity,
        .minimumAngularVelocity =
            config::mirrorSwapSmokeMinimumAngularVelocity,
        .maximumAngularVelocity =
            config::mirrorSwapSmokeMaximumAngularVelocity,
        .drawOnTop = config::mirrorSwapSmokeDrawOnTop,
    };
    effect.textures.reserve(config::mirrorSwapSmokeTextureNames.size());
    for (std::string_view name : config::mirrorSwapSmokeTextureNames) {
        effect.textures.push_back(manifest.textureIdByName(name));
    }
    return effect;
}

} // namespace sokoban
