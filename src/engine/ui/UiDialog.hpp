#pragma once

#include "engine/ui/Ui.hpp"

#include <string_view>

namespace sokoban {

// Shared placement and chrome for a centered native dialog. The caller owns
// focus, the contents, and the decision to dismiss the dialog.
struct UiDialogLayout {
    UiRect panel;
    UiRect content;
    float scale = 0.0f;

    [[nodiscard]] static UiDialogLayout centered(
        Vec2 viewport,
        float scale,
        Vec2 preferredSize,
        float fittingHeight);
    [[nodiscard]] UiRect footerButton(float width, float offsetFromRight = 0.0f)
        const;
    void draw(UiContext& ui, std::string_view title) const;
};

// Also used when a dialog is docked beside a live scene preview.
void drawDialogPanel(
    UiContext& ui,
    UiRect panel,
    std::string_view title,
    float inset,
    float scale);

} // namespace sokoban
