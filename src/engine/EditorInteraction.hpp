#pragma once

#include "engine/DecorationGizmo.hpp"
#include "engine/SplatCanvas.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
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

    struct SelectorLabel {
        uint32_t id = 0;
        std::string text;
        Vec2 anchor;

        bool operator==(const SelectorLabel&) const = default;
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

    [[nodiscard]] static std::vector<SelectorLabel> selectorLabels(
        const std::vector<Level::ScreenSelector>& selectors,
        const ProjectToPixels& project);
};

} // namespace sokoban
