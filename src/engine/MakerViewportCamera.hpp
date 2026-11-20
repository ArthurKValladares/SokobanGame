#pragma once

#include "engine/ui/Ui.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace sokoban {

// View-only state. The authored camera still owns the ordinary isometric angle.
class MakerViewportCamera {
public:
    // The board's footprint as it was drawn: its four corners in order around
    // the edge, in pixels. A convex quadrilateral under any camera angle.
    using Footprint = std::array<Vec2, 4>;

    // How far inside the viewport, as a share of its size on each axis, the
    // board must still reach after a gesture. The board cursor is revealed
    // with the same inset.
    static constexpr float keepInViewFraction = 0.12f;

    // Trims a pan so that the board still reaches the viewport's inner
    // rectangle, the one inset by keepInViewFraction on every side. `board`
    // moves with the view, so it is tested where the pan would leave it.
    // Without this a long drag carried the board out of sight and left nothing
    // on screen to say which way it had gone. The shape is tested rather than
    // its bounding box, because the corners of a bounding box are empty when
    // the board is seen at an angle. A zero delta returns the pan that brings
    // a board already out of reach back in.
    [[nodiscard]] static Vec2 limitPan(Vec2 pixelDelta, const Footprint& board, UiRect viewport)
    {
        const Vec2 inset = viewport.size * keepInViewFraction;
        const Vec2 low = viewport.position + inset;
        const Vec2 high = viewport.position + viewport.size - inset;
        Footprint moved {};
        for (std::size_t i = 0; i < board.size(); ++i) {
            moved[i] = board[i] + pixelDelta;
            // A footprint that could not be measured limits nothing.
            if (!std::isfinite(moved[i].x) || !std::isfinite(moved[i].y)) return pixelDelta;
        }
        if (!(low.x <= high.x) || !(low.y <= high.y)) return pixelDelta;
        const Footprint inner { low, Vec2 { high.x, low.y }, high, Vec2 { low.x, high.y } };
        if (overlaps(moved, inner)) return pixelDelta;

        // Two convex shapes that do not overlap are closest at a corner of one
        // of them, so the shortest way back is the shortest of these.
        Vec2 shortest {};
        float shortestSquared = std::numeric_limits<float>::max();
        const auto consider = [&](Vec2 offset) {
            const float squared = dot(offset, offset);
            if (squared < shortestSquared) {
                shortestSquared = squared;
                shortest = offset;
            }
        };
        for (const Vec2 corner : moved) {
            consider(Vec2 { std::clamp(corner.x, low.x, high.x), std::clamp(corner.y, low.y, high.y) } - corner);
        }
        for (const Vec2 corner : inner) {
            for (std::size_t i = 0; i < moved.size(); ++i) {
                consider(corner - closestOnSegment(corner, moved[i], moved[(i + 1) % moved.size()]));
            }
        }
        return pixelDelta + shortest;
    }

    void pan(Vec2 pixelDelta, Vec2 viewportSize)
    {
        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) return;
        setOffset(transform_.clipOffset + Vec2 {
            2.0f * pixelDelta.x / viewportSize.x,
            -2.0f * pixelDelta.y / viewportSize.y,
        });
    }

    // Scales the view about the pointer. Returns the factor it was scaled by,
    // which is 1 when the zoom was already at its limit.
    float zoom(float wheelDelta, Vec2 pointerPosition, UiRect viewport)
    {
        if (!std::isfinite(wheelDelta) || viewport.size.x <= 0.0f || viewport.size.y <= 0.0f) return 1.0f;
        const float nextZoom = std::clamp(transform_.zoom * std::pow(1.2f, wheelDelta), 0.25f, 8.0f);
        if (nextZoom == transform_.zoom) return 1.0f;
        const float factor = nextZoom / transform_.zoom;
        const Vec2 pointer = pointerPosition - viewport.position;
        const Vec2 clip {
            2.0f * pointer.x / viewport.size.x - 1.0f,
            1.0f - 2.0f * pointer.y / viewport.size.y,
        };
        setOffset(clip + (transform_.clipOffset - clip) * factor);
        transform_.zoom = nextZoom;
        return factor;
    }

    void toggleTopDown() { topDown_ = !topDown_; }
    [[nodiscard]] bool topDown() const { return topDown_; }

    void frameSelection(GridPosition3 first, GridPosition3 last)
    {
        const auto [left, right] = std::minmax(first.x, last.x);
        const auto [nearY, farY] = std::minmax(first.y, last.y);
        const auto [bottom, top] = std::minmax(first.z, last.z);
        const auto width = static_cast<int64_t>(right) - left + 1;
        const auto height = static_cast<int64_t>(farY) - nearY + 1;
        const auto depth = static_cast<int64_t>(top) - bottom + 1;
        if (!std::in_range<uint32_t>(width) || !std::in_range<uint32_t>(height) ||
            !std::in_range<uint32_t>(depth)) return;
        transform_ = {};
        selection_ = RenderFrameData::CameraExtent {
            left, nearY, bottom,
            static_cast<uint32_t>(width), static_cast<uint32_t>(height), static_cast<uint32_t>(depth),
        };
    }

    void resetFraming()
    {
        transform_ = {};
        selection_.reset();
    }

    void apply(RenderFrameData& frame) const
    {
        if (selection_) {
            const auto board = frame.cameraExtent.value_or(RenderFrameData::CameraExtent {
                .width = frame.levelWidth, .height = frame.levelHeight, .depth = frame.levelDepth,
            });
            // Fitting a small region must not move the camera inside the rest
            // of a tall board. Keep the board-sized lens distance when zoomed in.
            const auto boardSpan = std::max({ board.width, board.height, board.depth });
            const auto selectionSpan = std::max(selection_->width, selection_->height);
            frame.cameraDistanceMultiplier = frame.cameraDistanceMultiplier.value_or(1.0f) *
                std::max(1.0f, static_cast<float>(boardSpan) / static_cast<float>(selectionSpan));
            frame.cameraExtent = selection_;
        }
        if (topDown_) {
            frame.cameraPitchDegrees = 0.0f;
            frame.cameraYawDegrees = 0.0f;
        }
        if (transform_.zoom != 1.0f || transform_.clipOffset != Vec2 {}) {
            frame.viewportCamera = transform_;
        }
    }

private:
    [[nodiscard]] static float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }

    [[nodiscard]] static Vec2 closestOnSegment(Vec2 point, Vec2 from, Vec2 to)
    {
        const Vec2 along = to - from;
        const float lengthSquared = dot(along, along);
        if (lengthSquared <= 0.0f) return from;
        return from + along * std::clamp(dot(point - from, along) / lengthSquared, 0.0f, 1.0f);
    }

    // Inside or on the edge of a convex quadrilateral, whichever way round its
    // corners run.
    [[nodiscard]] static bool contains(const Footprint& shape, Vec2 point)
    {
        bool left = false;
        bool right = false;
        for (std::size_t i = 0; i < shape.size(); ++i) {
            const float side = cross(shape[(i + 1) % shape.size()] - shape[i], point - shape[i]);
            left = left || side > 0.0f;
            right = right || side < 0.0f;
        }
        return !(left && right);
    }

    [[nodiscard]] static bool segmentsCross(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
    {
        const float c1 = cross(b - a, c - a);
        const float c2 = cross(b - a, d - a);
        const float c3 = cross(d - c, a - c);
        const float c4 = cross(d - c, b - c);
        return ((c1 > 0.0f) != (c2 > 0.0f)) && ((c3 > 0.0f) != (c4 > 0.0f));
    }

    // Two convex quadrilaterals overlap when a corner of one is inside the
    // other or two of their edges cross.
    [[nodiscard]] static bool overlaps(const Footprint& first, const Footprint& second)
    {
        for (const Vec2 corner : first) {
            if (contains(second, corner)) return true;
        }
        for (const Vec2 corner : second) {
            if (contains(first, corner)) return true;
        }
        for (std::size_t i = 0; i < first.size(); ++i) {
            for (std::size_t j = 0; j < second.size(); ++j) {
                if (segmentsCross(first[i], first[(i + 1) % first.size()],
                        second[j], second[(j + 1) % second.size()])) {
                    return true;
                }
            }
        }
        return false;
    }

    void setOffset(Vec2 offset)
    {
        if (std::isfinite(offset.x) && std::isfinite(offset.y)) transform_.clipOffset = offset;
    }

    RenderFrameData::ViewportCameraTransform transform_;
    std::optional<RenderFrameData::CameraExtent> selection_;
    bool topDown_ = false;
};

} // namespace sokoban
