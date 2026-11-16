#pragma once

#include "engine/ui/Ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

namespace sokoban {

// View-only state. The authored camera still owns the ordinary isometric angle.
class MakerViewportCamera {
public:
    void pan(Vec2 pixelDelta, Vec2 viewportSize)
    {
        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) return;
        setOffset(transform_.clipOffset + Vec2 {
            2.0f * pixelDelta.x / viewportSize.x,
            -2.0f * pixelDelta.y / viewportSize.y,
        });
    }

    void zoom(float wheelDelta, Vec2 pointerPosition, UiRect viewport)
    {
        if (!std::isfinite(wheelDelta) || viewport.size.x <= 0.0f || viewport.size.y <= 0.0f) return;
        const float nextZoom = std::clamp(transform_.zoom * std::pow(1.2f, wheelDelta), 0.25f, 8.0f);
        if (nextZoom == transform_.zoom) return;
        const Vec2 pointer = pointerPosition - viewport.position;
        const Vec2 clip {
            2.0f * pointer.x / viewport.size.x - 1.0f,
            1.0f - 2.0f * pointer.y / viewport.size.y,
        };
        setOffset(clip + (transform_.clipOffset - clip) * (nextZoom / transform_.zoom));
        transform_.zoom = nextZoom;
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
    void setOffset(Vec2 offset)
    {
        if (std::isfinite(offset.x) && std::isfinite(offset.y)) transform_.clipOffset = offset;
    }

    RenderFrameData::ViewportCameraTransform transform_;
    std::optional<RenderFrameData::CameraExtent> selection_;
    bool topDown_ = false;
};

} // namespace sokoban
