#include "TestAssetRoot.hpp"
#include "TestHarness.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/GroundGeometry.hpp"
#include "engine/PerformanceFixtures.hpp"
#include "engine/PresentationSettings.hpp"

#include <algorithm>
#include <iostream>

int main()
{
    const auto manifest = sokoban::AssetManifest::loadFromFile(
        testAssetRoot() / "manifest.json");
    for (const auto scenario : { "mirror-swap", "witch-swap", "turret-volley",
             "portals", "special-blocks", "mixed-stress" }) {
        sokoban::PerformanceEffectFixture fixture(manifest);
        sokoban::RenderFrameData frame;
        frame.levelWidth = frame.levelHeight = 16;
        fixture.append(frame, manifest, scenario, 8);
        CHECK(!frame.particles.empty());
        CHECK(std::ranges::all_of(frame.particles, [](const auto& particle) {
            return !particle.texture.isNone() && particle.size.x > 0.0f &&
                particle.size.y > 0.0f && particle.color.w > 0.0f;
        }));
        if (std::string_view(scenario) == "turret-volley") {
            CHECK(std::ranges::any_of(frame.particles, [](const auto& particle) {
                return particle.billboardAlignmentUsesY;
            }));
        }
        if (std::string_view(scenario) == "portals" ||
            std::string_view(scenario) == "mixed-stress") {
            CHECK(std::ranges::any_of(frame.isoFaces, [](const auto& face) {
                return face.effect == sokoban::RenderSurfaceEffect::PortalEnergy;
            }));
            CHECK(std::ranges::any_of(frame.isoFaces, [](const auto& face) {
                return face.effect == sokoban::RenderSurfaceEffect::PortalBeam;
            }));
            CHECK(std::ranges::all_of(frame.isoFaces, [](const auto& face) {
                return (face.effect != sokoban::RenderSurfaceEffect::PortalEnergy &&
                           face.effect != sokoban::RenderSurfaceEffect::PortalBeam) ||
                    (face.translucent && !face.castsShadows);
            }));
        }
        if (std::string_view(scenario) == "special-blocks" ||
            std::string_view(scenario) == "mixed-stress") {
            for (const auto effect : { sokoban::RenderSurfaceEffect::GateEnergy,
                     sokoban::RenderSurfaceEffect::MirrorEnergy,
                     sokoban::RenderSurfaceEffect::LinkedObjectAura }) {
                CHECK(std::ranges::any_of(frame.tiles, [effect](const auto& tile) {
                    return tile.effect == effect;
                }));
            }
            CHECK(std::ranges::any_of(frame.tiles, [](const auto& tile) {
                return tile.blurBehind && !tile.model.isCube();
            }));
        }
        const auto frozenCount = frame.particles.size();
        const auto frozenFaceCount = frame.isoFaces.size();
        frame.tiles.clear();
        frame.particles.clear();
        frame.isoFaces.clear();
        fixture.append(frame, manifest, scenario, 8);
        CHECK(frame.particles.size() == frozenCount);
        CHECK(frame.isoFaces.size() == frozenFaceCount);
    }
    {
        sokoban::RockScratchEvidenceFixture fixture;
        sokoban::PresentationSettings settings;
        sokoban::GroundGeometryCache groundCache;
        sokoban::FrameArena arena("rock scratch evidence", sokoban::renderFrameArenaBytes());
        const auto frame = fixture.build(manifest, settings, groundCache, arena);
        CHECK(frame.rockScratchCount == 8);
        CHECK(!frame.waterSurfaces.empty());
        CHECK(frame.rockScratches.front().ageSeconds > 3.0f);
        if (frame.rockScratchCount > 0) {
            CHECK(frame.rockScratches[frame.rockScratchCount - 1].ageSeconds < 0.7f);
        }
        for (std::size_t index = 1; index < frame.rockScratchCount; ++index) {
            CHECK(frame.rockScratches[index - 1].ageSeconds >=
                frame.rockScratches[index].ageSeconds);
        }
        CHECK(std::ranges::any_of(frame.rockScratches.begin(),
            frame.rockScratches.begin() + frame.rockScratchCount, [](const auto& scratch) {
                return scratch.end.x > scratch.start.x;
            }));
        CHECK(std::ranges::any_of(frame.rockScratches.begin(),
            frame.rockScratches.begin() + frame.rockScratchCount, [](const auto& scratch) {
                return scratch.end.y < scratch.start.y;
            }));
    }
    if (failures) return 1;
    std::cout << "Performance effect fixtures passed\n";
    return 0;
}
