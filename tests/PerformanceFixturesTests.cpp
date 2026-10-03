#include "TestAssetRoot.hpp"
#include "TestHarness.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/PerformanceFixtures.hpp"

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
        frame.tiles.clear();
        frame.particles.clear();
        fixture.append(frame, manifest, scenario, 8);
        CHECK(frame.particles.size() == frozenCount);
    }
    if (failures) return 1;
    std::cout << "Performance effect fixtures passed\n";
    return 0;
}
