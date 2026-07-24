#include "engine/render/RendererReconfiguration.hpp"

#include "engine/render/RenderResolution.hpp"

namespace sokoban {

RendererReconfigurationQueue::RendererReconfigurationQueue(
    RendererSettingsSnapshot initial)
    : active_(initial)
    , requested_(initial)
{
    active_.renderScalePercent =
        normalizedRenderScalePercent(active_.renderScalePercent);
    requested_ = active_;
}

void RendererReconfigurationQueue::requestAntiAliasing(
    AntiAliasingMode mode)
{
    requested_.antiAliasing = mode;
}

void RendererReconfigurationQueue::requestRenderScalePercent(int percent)
{
    requested_.renderScalePercent =
        normalizedRenderScalePercent(percent);
}

void RendererReconfigurationQueue::requestWireframe(bool enabled)
{
    requested_.wireframe = enabled;
}

const RendererSettingsSnapshot&
RendererReconfigurationQueue::active() const
{
    return active_;
}

const RendererSettingsSnapshot&
RendererReconfigurationQueue::requested() const
{
    return requested_;
}

std::optional<RendererReconfigurationPlan>
RendererReconfigurationQueue::plan(bool recreateSwapchain) const
{
    const bool rebuildRenderResources =
        recreateSwapchain ||
        requested_.antiAliasing != active_.antiAliasing ||
        requested_.renderScalePercent !=
            active_.renderScalePercent;
    const bool rebuildPipelines =
        rebuildRenderResources ||
        requested_.wireframe != active_.wireframe;
    if (!rebuildPipelines && !recreateSwapchain) {
        return std::nullopt;
    }
    return RendererReconfigurationPlan {
        .settings = requested_,
        .rebuildRenderResources = rebuildRenderResources,
        .rebuildPipelines = rebuildPipelines,
        .recreateSwapchain = recreateSwapchain,
    };
}

void RendererReconfigurationQueue::commit(
    const RendererReconfigurationPlan& plan)
{
    active_ = plan.settings;
}

} // namespace sokoban
