#pragma once

#include "engine/DecorationGizmo.hpp"
#include "engine/SplatCanvas.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace sokoban {

// Headless geometry and coordinate policy for editor interaction overlays.
// The renderer adapter supplies world-to-pixel projection; this module owns
// sampling, topology, and scale so those rules can be tested without Vulkan
// or ImGui.
class EditorInteraction {
public:
    using ProjectToPixels = std::function<std::optional<Vec2>(Vec3)>;

    struct BrushVertex {
        Vec2 position;
        float opacity = 0.0f;
    };

    struct BrushPreview {
        std::vector<BrushVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<Vec2> rim;
    };

    [[nodiscard]] static BrushPreview brushPreview(
        const SplatCanvas::Brush& brush,
        Vec3 brushPoint,
        const ProjectToPixels& project);

    [[nodiscard]] static std::optional<DecorationGizmo::Geometry>
        decorationGizmoGeometry(
            const Level::Decoration& decoration,
            const ProjectToPixels& project);

    [[nodiscard]] static Vec2 pointerPixels(
        Vec2 pointer,
        Vec2 windowSize,
        Vec2 pixelSize);
};

} // namespace sokoban
