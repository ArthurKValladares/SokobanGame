#include "engine/ui/UiScroll.hpp"

#include "engine/ui/Ui.hpp"

namespace sokoban {

void UiScroll::scrollWheel(
    UiContext& ui, UiRect viewport, float delta, std::optional<Vec2> position,
    float pixelsPerStep)
{
    if (maxOffset_ > 0.0f && delta != 0.0f && !ui.mouseDown() &&
        ui.contains(ui.visibleRect(viewport), position.value_or(ui.mousePosition()))) {
        scrollBy(-delta * pixelsPerStep);
    }
}

void UiScroll::drawScrollbar(UiContext& ui, std::string_view id, UiRect track)
{
    if (maxOffset_ <= 0.0f || track.size.x <= 0.0f || track.size.y <= 0.0f) {
        scrollbarGrabOffset_.reset();
        return;
    }
    const float thumbHeight = std::clamp(
        track.size.y * viewportHeight_ / (viewportHeight_ + maxOffset_),
        std::min(20.0f, track.size.y),
        track.size.y);
    const float travel = track.size.y - thumbHeight;
    const auto thumb = [&] {
        return UiRect {
            { track.position.x, track.position.y + travel * offset_ / maxOffset_ },
            { track.size.x, thumbHeight },
        };
    };
    if (const auto press = ui.mousePressPosition();
        press && ui.clicked(track)) {
        const UiRect knob = thumb();
        scrollbarGrabOffset_ = ui.contains(knob, *press)
            ? press->y - knob.position.y : thumbHeight * 0.5f;
    }
    if (travel > 0.0f && scrollbarGrabOffset_) {
        if (const auto pointer = ui.dragPosition(id, track)) {
            const float position = pointer->y - track.position.y - *scrollbarGrabOffset_;
            offset_ = std::clamp(position / travel, 0.0f, 1.0f) * maxOffset_;
        }
    }
    if (!ui.mouseDown()) {
        scrollbarGrabOffset_.reset();
    }
    ui.rect(track, { 0.20f, 0.23f, 0.23f, 1.0f });
    ui.rect(thumb(), { 0.40f, 0.56f, 0.53f, 1.0f });
}

} // namespace sokoban
