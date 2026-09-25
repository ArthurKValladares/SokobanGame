#include "TestHarness.hpp"

#include "engine/render/RendererReconfiguration.hpp"

#include <iostream>

namespace {

void testCoalescesSettingsIntoOneRenderResourcePlan()
{
    sokoban::RendererReconfigurationQueue queue({
        .antiAliasing = sokoban::AntiAliasingMode::Msaa8x,
        .renderScalePercent = 100,
        .wireframe = false,
    });
    queue.requestAntiAliasing(
        sokoban::AntiAliasingMode::Msaa4x);
    queue.requestRenderScalePercent(50);
    queue.requestWireframe(true);

    const auto plan = queue.plan(false);
    CHECK(plan.has_value());
    CHECK(plan->rebuildRenderResources);
    CHECK(plan->rebuildPipelines);
    CHECK(!plan->recreateSwapchain);
    CHECK(plan->settings.antiAliasing ==
        sokoban::AntiAliasingMode::Msaa4x);
    CHECK(plan->settings.renderScalePercent == 50);
    CHECK(plan->settings.wireframe);
    CHECK(queue.active().renderScalePercent == 100);
    CHECK(queue.requested().renderScalePercent == 50);

    queue.commit(*plan);
    CHECK(queue.active() == queue.requested());
    CHECK(!queue.plan(false).has_value());
}

void testWireframeOnlyRebuildsPipelines()
{
    sokoban::RendererReconfigurationQueue queue({
        .antiAliasing = sokoban::AntiAliasingMode::Msaa4x,
        .renderScalePercent = 75,
        .wireframe = false,
    });
    queue.requestWireframe(true);
    const auto plan = queue.plan(false);
    CHECK(plan.has_value());
    CHECK(!plan->rebuildRenderResources);
    CHECK(plan->rebuildPipelines);
}

void testSwapchainRequestForcesFullReplacement()
{
    sokoban::RendererReconfigurationQueue queue({
        .antiAliasing = sokoban::AntiAliasingMode::None,
        .renderScalePercent = 100,
        .wireframe = false,
    });
    const auto plan = queue.plan(true);
    CHECK(plan.has_value());
    CHECK(plan->recreateSwapchain);
    CHECK(plan->rebuildRenderResources);
    CHECK(plan->rebuildPipelines);
}

void testRequestsCanReturnToActiveWithoutWork()
{
    sokoban::RendererReconfigurationQueue queue({
        .antiAliasing = sokoban::AntiAliasingMode::Msaa2x,
        .renderScalePercent = 67,
        .wireframe = false,
    });
    queue.requestRenderScalePercent(10);
    CHECK(queue.requested().renderScalePercent == 25);
    queue.requestRenderScalePercent(67);
    queue.requestWireframe(true);
    queue.requestWireframe(false);
    CHECK(!queue.plan(false).has_value());
}

void testShaderReloadOnlyRebuildsPipelines()
{
    sokoban::RendererReconfigurationQueue queue({
        .antiAliasing = sokoban::AntiAliasingMode::Msaa4x,
        .renderScalePercent = 100,
        .wireframe = false,
    });
    queue.requestShaderReload();
    const auto plan = queue.plan(false);
    CHECK(plan.has_value());
    CHECK(plan->rebuildPipelines);
    CHECK(!plan->rebuildRenderResources);
    CHECK(!plan->recreateSwapchain);
    queue.commit(*plan);
    CHECK(queue.active().shaderRevision == 1);
    CHECK(!queue.plan(false).has_value());

    // Two reloads before one frame boundary are one rebuild, and a reload
    // requested after a commit is new work even though the count matches.
    queue.requestShaderReload();
    queue.requestShaderReload();
    const auto coalesced = queue.plan(false);
    CHECK(coalesced.has_value());
    queue.commit(*coalesced);
    CHECK(!queue.plan(false).has_value());
    queue.requestShaderReload();
    CHECK(queue.plan(false).has_value());
}

} // namespace

int main()
{
    testCoalescesSettingsIntoOneRenderResourcePlan();
    testWireframeOnlyRebuildsPipelines();
    testSwapchainRequestForcesFullReplacement();
    testRequestsCanReturnToActiveWithoutWork();
    testShaderReloadOnlyRebuildsPipelines();

    if (failures == 0) {
        std::cout << "RendererReconfigurationTests: "
                  << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "RendererReconfigurationTests: "
              << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
