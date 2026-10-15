#include "engine/PerformanceFixtures.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/GateEffect.hpp"
#include "engine/GameplayLoop.hpp"
#include "engine/ParticleConfig.hpp"
#include "engine/RenderFrameParts.hpp"
#include "engine/RenderFrameBuilder.hpp"

#include <algorithm>
#include <stdexcept>

namespace sokoban {

RockScratchEvidenceFixture::RockScratchEvidenceFixture(const AnimationCatalog* animations)
    : level_(Level::loadFromDefinition({
          .layers = {
              { "         ", " ....... ", " ....... ", " ....... ",
                  " ....... ", " ....... ", "         " },
              { "         ", "         ", "         ", "         ",
                  " KR      ", "         ", "         " },
          },
          .waterLayer = 0,
      }, "rock scratch evidence"))
    , animations_(animations)
{
    session_.reset(level_);
    session_.setStepDurationSeconds(0.2f);
    presentation_.setAnimationCatalog(animations_);
    presentation_.resetEntities(session_.state());

    const auto advance = [&](float seconds, const GameplayLoop::InputFrame& input = {}) {
        (void)GameplayLoop::update(level_, session_, presentation_, input, 0.0f, false);
        // Exercise ordinary presentation sampling during movement, rather than
        // jumping over the contacts that lay the scratches down.
        for (; seconds > 0.0001f; seconds -= 1.0f / 60.0f) {
            (void)GameplayLoop::update(level_, session_, presentation_, {},
                std::min(seconds, 1.0f / 60.0f), false);
        }
    };
    advance(0.3f, { .right = { .pressed = true } });
    advance(0.3f, { .right = { .pressed = true } });
    advance(1.2f);
    advance(0.3f, { .down = { .pressed = true } });
    advance(0.3f, { .right = { .pressed = true } });
    advance(0.3f, { .up = { .pressed = true } });
    advance(0.3f, { .up = { .pressed = true } });
    // Step aside to leave the new end of the trail visible beside the rock.
    advance(0.3f, { .left = { .pressed = true } });
}

RenderFrameData RockScratchEvidenceFixture::build(const AssetManifest& manifest,
    const PresentationSettings& settings, GroundGeometryCache& groundCache,
    FrameArena& arena) const
{
    return RenderFrameBuilder::buildGameplay({
        .manifest = manifest,
        .level = level_,
        .state = session_.state(),
        .activeHeroController = session_.activeHeroController(),
        .moving = session_.moving(),
        .projectedState = session_.projectedStateView(),
        .presentation = presentation_,
        .settings = settings,
        .animations = animations_,
        .groundGeometryCache = &groundCache,
    }, arena);
}

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
