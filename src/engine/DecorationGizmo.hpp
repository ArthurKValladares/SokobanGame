#pragma once

#include "engine/Level.hpp"
#include "engine/Math.hpp"

#include <array>
#include <optional>
#include <vector>

namespace sokoban {

// Vulkan- and UI-free transform manipulator. Presentation code supplies the
// projected handles; this class owns hit testing and drag semantics.
class DecorationGizmo {
public:
    enum class Mode {
        Translate,
        Rotate,
        Scale,
    };

    enum class Axis {
        X,
        Y,
        Z,
    };

    struct AxisHandle {
        Vec2 start;
        Vec2 end;
        float worldLength = 1.0f;
    };

    struct Geometry {
        Vec2 origin;
        std::array<AxisHandle, 3> axes;
        std::array<std::vector<Vec2>, 3> rings;
    };

    void setMode(Mode mode);
    [[nodiscard]] Mode mode() const;
    [[nodiscard]] std::optional<Axis> hoveredAxis(
        const Geometry& geometry,
        Vec2 pointer) const;
    [[nodiscard]] bool beginDrag(
        const Geometry& geometry,
        Vec2 pointer,
        const Level::Decoration& decoration);
    [[nodiscard]] std::optional<Level::Decoration> updateDrag(
        Vec2 pointer) const;
    void endDrag();
    [[nodiscard]] bool dragging() const;
    [[nodiscard]] std::optional<Axis> activeAxis() const;

private:
    struct DragState {
        Axis axis = Axis::X;
        Vec2 startPointer;
        Vec2 pixelDirection { 1.0f, 0.0f };
        float unitsPerPixel = 1.0f;
        Level::Decoration startDecoration;
    };

    Mode mode_ = Mode::Translate;
    std::optional<DragState> drag_;
};

} // namespace sokoban
