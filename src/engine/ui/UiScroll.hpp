#pragma once

#include "engine/Math.hpp"

#include <algorithm>
#include <optional>
#include <string_view>

namespace sokoban {

class UiContext;
struct UiRect;

// Vertical scroll state in UI pixels. Item positions are relative to the
// start of the unscrolled content.
class UiScroll {
public:
    void setExtents(float viewportHeight, float contentHeight)
    {
        viewportHeight_ = std::max(viewportHeight, 0.0f);
        maxOffset_ = std::max(contentHeight - viewportHeight_, 0.0f);
        offset_ = std::min(offset_, maxOffset_);
        if (maxOffset_ == 0.0f) {
            scrollbarGrabOffset_.reset();
        }
    }

    void scrollBy(float pixels)
    {
        offset_ = std::clamp(offset_ + pixels, 0.0f, maxOffset_);
    }

    void reveal(float top, float bottom)
    {
        if (top < offset_ || bottom - top > viewportHeight_) {
            offset_ = std::clamp(top, 0.0f, maxOffset_);
        } else if (bottom > offset_ + viewportHeight_) {
            offset_ = std::min(bottom - viewportHeight_, maxOffset_);
        }
    }

    // A positive wheel delta scrolls towards the top. Supply the wheel's
    // event position when available, since the pointer can move afterwards.
    void scrollWheel(
        UiContext& ui, UiRect viewport, float delta,
        std::optional<Vec2> position = {}, float pixelsPerStep = 52.0f);
    void drawScrollbar(UiContext& ui, std::string_view id, UiRect track);

    [[nodiscard]] float offset() const { return offset_; }

private:
    float viewportHeight_ = 0.0f;
    float maxOffset_ = 0.0f;
    float offset_ = 0.0f;
    std::optional<float> scrollbarGrabOffset_;
};

} // namespace sokoban
