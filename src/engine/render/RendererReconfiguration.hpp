#pragma once

#include <optional>

namespace sokoban {

enum class AntiAliasingMode {
    None,
    Msaa2x,
    Msaa4x,
    Msaa8x,
};

struct RendererSettingsSnapshot {
    AntiAliasingMode antiAliasing = AntiAliasingMode::Msaa8x;
    int renderScalePercent = 100;
    bool wireframe = false;

    bool operator==(const RendererSettingsSnapshot&) const = default;
};

struct RendererReconfigurationPlan {
    RendererSettingsSnapshot settings;
    bool rebuildRenderResources = false;
    bool rebuildPipelines = false;
    bool recreateSwapchain = false;
};

// Coalesces any number of UI/settings writes into one frame-boundary plan.
// Requested values are visible immediately; active values advance only after
// Vulkan replacement resources have been created successfully.
class RendererReconfigurationQueue {
public:
    explicit RendererReconfigurationQueue(
        RendererSettingsSnapshot initial);

    void requestAntiAliasing(AntiAliasingMode mode);
    void requestRenderScalePercent(int percent);
    void requestWireframe(bool enabled);

    [[nodiscard]] const RendererSettingsSnapshot& active() const;
    [[nodiscard]] const RendererSettingsSnapshot& requested() const;
    [[nodiscard]] std::optional<RendererReconfigurationPlan> plan(
        bool recreateSwapchain) const;
    void commit(const RendererReconfigurationPlan& plan);

private:
    RendererSettingsSnapshot active_;
    RendererSettingsSnapshot requested_;
};

} // namespace sokoban
