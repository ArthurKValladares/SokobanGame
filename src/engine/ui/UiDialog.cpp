#include "engine/ui/UiDialog.hpp"

#include <algorithm>
#include <cmath>

namespace sokoban {

UiDialogLayout UiDialogLayout::centered(Vec2 viewport, float scale, Vec2 preferredSize, float fittingHeight)
{
    scale = std::min({ scale, viewport.x / 360.0f, viewport.y / fittingHeight });
    if (!std::isfinite(scale) || scale <= 0.0f) return {};
    const Vec2 size {
        std::min(preferredSize.x * scale, viewport.x - 24.0f * scale),
        std::min(preferredSize.y * scale, viewport.y - 24.0f * scale),
    };
    const UiRect panel { (viewport - size) * 0.5f, size };
    return {
        .panel = panel,
        .content = { panel.position + Vec2 { 20.0f, 65.0f } * scale,
            { size.x - 40.0f * scale, std::max(0.0f, size.y - 131.0f * scale) } },
        .scale = scale,
    };
}

UiRect UiDialogLayout::footerButton(float width, float offsetFromRight) const
{
    return { { content.position.x + content.size.x - (width + offsetFromRight) * scale,
                 panel.position.y + panel.size.y - 54.0f * scale },
        { width * scale, 34.0f * scale } };
}

void UiDialogLayout::draw(UiContext& ui, std::string_view title) const
{
    ui.rect({ {}, ui.drawData().viewportSize }, { 0.012f, 0.016f, 0.017f, 0.65f });
    drawDialogPanel(ui, panel, title, 20.0f * scale, scale);
}

void drawDialogPanel(UiContext& ui, UiRect panel, std::string_view title, float inset, float scale)
{
    constexpr Vec4 ink { 0.94f, 0.95f, 0.90f, 1.0f };
    const float left = panel.position.x + inset;
    ui.panel(panel);
    ui.text({ left, panel.position.y + 15.0f * scale }, title, ink, 25.0f * scale);
    ui.divider({ { left, panel.position.y + 51.0f * scale }, { panel.size.x - 2.0f * inset, scale } });
}

} // namespace sokoban
