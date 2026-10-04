#include "engine/PerformanceFixtures.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/GateEffect.hpp"
#include "engine/ParticleConfig.hpp"
#include "engine/RenderFrameParts.hpp"

#include <algorithm>
#include <stdexcept>

namespace sokoban {

PerformanceEffectFixture::PerformanceEffectFixture(const AssetManifest& manifest)
    : mirror_(makeMirrorSwapParticleEffect(manifest))
    , witch_(makeWitchSwapParticleEffect(manifest))
    , turret_(makeTurretParticleEffects(manifest))
{
}

void PerformanceEffectFixture::append(RenderFrameData& frame,
    const AssetManifest& manifest, std::string_view scenario, uint32_t emitters)
{
    if (!isPerformanceEffectScenario(scenario)) {
        throw std::invalid_argument("Unknown performance effect scenario");
    }
    particles_.reset();
    const bool mixed = scenario == "mixed-stress";
    const int width = static_cast<int>(std::max(frame.levelWidth, 4U));
    const int height = static_cast<int>(std::max(frame.levelHeight, 4U));
    const PresentationSettings settings;
    for (uint32_t index = 0; index < emitters; ++index) {
        const GridPosition3 cell {
            1 + static_cast<int>(index % static_cast<uint32_t>(width - 2)),
            1 + static_cast<int>((index / static_cast<uint32_t>(width - 2)) %
                static_cast<uint32_t>(height - 2)), 1 };
        const Vec3 origin { static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.y) + 0.5f, 1.5f };
        if (mixed || scenario == "mirror-swap") {
            particles_.emit(origin, mirror_);
        }
        if (mixed || scenario == "witch-swap") {
            particles_.emit(origin, witch_);
        }
        if (mixed || scenario == "turret-volley") {
            const rules::TurretShot shot {
                .turretCell = cell,
                .targetCell = { width - 2, cell.y, 1 },
                .direction = MoveDirection::Right,
            };
            (void)emitTurretShotParticles(particles_, turret_, shot, 0.04f);
        }
        if (mixed || scenario == "portals") {
            auto portal = tileVisual(TileType::PortalNorth, cell, manifest, settings);
            portal.color = { 0.2f, 0.7f, 1.0f, 1.0f };
            renderFrameParts::appendPortalVisual(frame, portal, TileType::PortalNorth,
                0.25f, manifest.textureIdByName(config::turretGlowTextureName));
        }
        if (mixed || scenario == "special-blocks") {
            auto ice = tileVisual(TileType::Ice, cell, manifest, settings);
            ice.blurBehind = true;
            frame.tiles.push_back(ice);
            auto rock = tileVisual(TileType::Rock, cell, manifest, settings);
            rock.position.x += 1.0f;
            frame.tiles.push_back(rock);
            renderFrameParts::appendLinkedObjectAura(frame, rock, { 1.0f, 0.7f, 0.1f });
            auto ghost = rock;
            ghost.position.y += 1.0f;
            ghost.effect = RenderSurfaceEffect::MirrorEnergy;
            ghost.color.w = 0.4f;
            frame.tiles.push_back(ghost);
            appendGateEffect(frame, Level::Gate {
                .cell = { cell.x, cell.y + 1, 1 },
                .color = { 0.2f, 0.7f, 1.0f },
            }, manifest, 1.0f, 0.25f);
        }
    }
    particles_.update(0.08f);
    particles_.appendRenderData(frame);
}

} // namespace sokoban
