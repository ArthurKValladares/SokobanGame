#include "TestHarness.hpp"

#include "engine/ui/FontAtlas.hpp"
#include "engine/ui/MakerView.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>

namespace {

using namespace sokoban;

bool intersects(UiRect first, UiRect second)
{
    return first.position.x < second.position.x + second.size.x &&
        second.position.x < first.position.x + first.size.x &&
        first.position.y < second.position.y + second.size.y &&
        second.position.y < first.position.y + first.size.y;
}

void testBoardStaysOpen(const FontAtlas& font)
{
    TEST("makerChromeLeavesBoardViewportOpen");
    UiContext ui(font);
    MakerView view;
    constexpr std::array viewports { Vec2 { 640.0f, 480.0f }, Vec2 { 1280.0f, 720.0f } };
    for (Vec2 viewport : viewports) {
        const MakerLayout layout = MakerView::layout(viewport);
        CHECK(layout.board.size.x >= 300.0f);
        CHECK(layout.board.size.y >= 300.0f);
        CHECK(!intersects(layout.board, layout.palette));
        CHECK(!intersects(layout.board, layout.toolbar));
        CHECK(!intersects(layout.board, layout.footer));
        ui.beginFrame(viewport, UiPointerInput {});
        (void)view.draw(ui, layout, {});
        ui.endFrame();
        CHECK(std::ranges::none_of(ui.drawData().commands, [&](const UiDrawCommand& command) {
            return intersects(command.rect, layout.board) &&
                (!command.clip || intersects(*command.clip, layout.board));
        }));
    }
}

void testPalettePointerOwnership(const FontAtlas& font)
{
    TEST("makerPaletteSelectsOnlyFromItsOwnPress");
    constexpr Vec2 viewport { 640.0f, 480.0f };
    const MakerLayout layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    const auto draw = [&](UiPointerInput pointer = {}, MakerViewInput input = {}) {
        ui.beginFrame(viewport, pointer);
        const auto action = view.draw(ui, layout, {}, input);
        ui.endFrame();
        return action;
    };
    const auto swatch = [&](TileType tile) -> std::optional<Vec2> {
        for (const auto& command : ui.drawData().commands) {
            if (command.kind == UiDrawKind::Solid && command.rect.size == Vec2 { 16.0f, 16.0f } &&
                command.color == tileColor(tile)) {
                const Vec2 center = command.rect.position + command.rect.size * 0.5f;
                if (command.clip && ui.contains(*command.clip, center)) return center;
            }
        }
        return std::nullopt;
    };
    (void)draw();
    const auto goal = swatch(TileType::End);
    CHECK(goal.has_value());
    if (!goal) return;
    const Vec2 board = layout.board.position + layout.board.size * 0.5f;
    CHECK(!draw({ .position = *goal, .down = true, .pressedAt = *goal }).tile);
    CHECK(!draw({ .position = board, .releasedAt = board }).tile);
    CHECK(!draw({ .position = board, .down = true, .pressedAt = board }).tile);
    CHECK(!draw({ .position = *goal, .releasedAt = *goal }).tile);
    CHECK(draw({ .position = *goal, .pressedAt = *goal, .releasedAt = *goal }).tile == TileType::End);

    (void)draw({ .position = board }, { .wheelY = -100.0f, .wheelPosition = board });
    CHECK(swatch(TileType::End).has_value());
    (void)draw({ .position = *goal }, { .wheelY = -100.0f, .wheelPosition = *goal });
    CHECK(!swatch(TileType::End));
}

void testDisplayScale(const FontAtlas& font)
{
    TEST("makerDisplayScalePreservesLogicalLayoutAndScroll");
    constexpr Vec2 logicalViewport { 640.0f, 480.0f };
    constexpr float scale = 2.0f;
    const Vec2 viewport = logicalViewport * scale;
    const MakerLayout logical = MakerView::layout(logicalViewport);
    const MakerLayout physical = MakerView::layout(viewport, scale);
    CHECK(physical.scale == scale);
    const auto pixels = [](UiRect rect) -> UiRect {
        return { rect.position * scale, rect.size * scale };
    };
    CHECK(physical.board == pixels(logical.board));
    CHECK(physical.palette == pixels(logical.palette));
    CHECK(physical.toolbar == pixels(logical.toolbar));
    CHECK(physical.footer == pixels(logical.footer));

    UiContext ui(font);
    MakerView view;
    const auto draw = [&](MakerViewInput input = {}) {
        ui.beginFrame(viewport, UiPointerInput {});
        (void)view.draw(ui, physical, {}, input);
        ui.endFrame();
    };
    const auto goal = [&]() -> std::optional<UiRect> {
        const auto swatch = std::ranges::find_if(ui.drawData().commands, [](const UiDrawCommand& command) {
            return command.kind == UiDrawKind::Solid && command.rect.size == Vec2 { 32.0f, 32.0f } &&
                command.color == tileColor(TileType::End);
        });
        if (swatch != ui.drawData().commands.end()) return swatch->rect;
        return std::nullopt;
    };
    draw();
    CHECK(std::ranges::none_of(ui.drawData().commands, [&](const UiDrawCommand& command) {
        return intersects(command.rect, physical.board) &&
            (!command.clip || intersects(*command.clip, physical.board));
    }));
    CHECK(std::ranges::any_of(ui.drawData().commands, [](const UiDrawCommand& command) {
        return command.kind == UiDrawKind::FontGlyph && command.fontSize == 48.0f;
    }));
    const auto firstGoal = goal();
    CHECK(firstGoal.has_value());
    if (!firstGoal) return;
    draw({ .wheelY = -1.0f, .wheelPosition = firstGoal->position });
    const auto shiftedGoal = goal();
    CHECK(shiftedGoal.has_value());
    if (shiftedGoal) CHECK(firstGoal->position.y - shiftedGoal->position.y == 52.0f * scale);
}

} // namespace

int main()
{
    const auto font = sokoban::FontAtlas::loadDefault(
        std::filesystem::path(SOKOBAN_TEST_ASSET_DIR));
    testBoardStaysOpen(font);
    testPalettePointerOwnership(font);
    testDisplayScale(font);
    std::cout << "MakerViewTests: " << checks - failures << " of " << checks
              << " checks passed\n";
    return failures != 0 ? 1 : 0;
}
