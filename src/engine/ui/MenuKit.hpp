#pragma once

#include "engine/ui/UiLayout.hpp"

#include <string>
#include <string_view>

namespace sokoban {

class UiContext;

// Shared building blocks for the player-facing menus (options, title,
// overlays). Everything here is presentation-only and headless; menus own
// their state and actions.
namespace menuKit {

// Builds a menu's focusable rows for one frame. Rows are appended (possibly
// conditionally) and each returns its index; navigation then wraps over
// exactly the rows that exist, so hand-maintained row enums and
// shifted-index arithmetic cannot drift out of sync with the layout.
class RowList {
public:
    // Returns the new row's index.
    [[nodiscard]] int add()
    {
        return count_++;
    }

    // Adds a row only when `present`; absent rows return -1, which never
    // matches a selection.
    [[nodiscard]] int addIf(bool present)
    {
        return present ? add() : -1;
    }

    [[nodiscard]] int count() const { return count_; }

    // Wrap-around navigation over the built rows, mutating the caller-owned
    // selection. Returns true when the selection moved (callers reset
    // row-local state such as a column focus on change).
    bool navigate(int& selectedRow, bool up, bool down) const
    {
        return navigateCount(selectedRow, count_, up, down);
    }

    // The same wrap navigation for pages whose row count is a fixed enum.
    static bool navigateCount(int& selectedRow, int rowCount, bool up, bool down);

private:
    int count_ = 0;
};

// Standard page scaffold: padded vertical tree with a title row, an optional
// subtitle row, and a divider. Pages append their content nodes to `tree`
// and call `tree.arrange(panel)` themselves.
struct MenuPage {
    explicit MenuPage(float afterHeader = 26.0f, bool withSubtitle = false);

    void drawHeader(
        UiContext& ui,
        std::string_view titleText,
        float titleSize = 40.0f,
        std::string_view subtitleText = {}) const;

    UiLayoutTree tree;
    UiLayoutNode title {};
    UiLayoutNode subtitle {};
    UiLayoutNode divider {};
    bool hasSubtitle = false;
};

// A centered panel clamped to the viewport (dialog-style pages).
[[nodiscard]] UiRect centeredPanel(
    Vec2 viewport,
    float desiredWidth,
    float desiredHeight,
    float minimumHeight);

// A centered full-height column (fullscreen pages such as the title).
[[nodiscard]] UiRect centeredColumn(Vec2 viewport, float desiredWidth);

// Right-aligned text inside a row, vertically centered.
void trailingText(
    UiContext& ui,
    UiRect row,
    std::string_view text,
    Vec4 color,
    float size,
    float rightPadding = 0.0f);

enum class DurationStyle {
    MinutesSeconds, // 1:23
    MinutesSecondsTenths, // 1:23.4
};

[[nodiscard]] std::string formatDuration(double seconds, DurationStyle style);

} // namespace menuKit
} // namespace sokoban
