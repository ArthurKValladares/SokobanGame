#include "TestHarness.hpp"

#include "engine/MakerSession.hpp"
#include "engine/ui/FontAtlas.hpp"
#include "engine/ui/MakerView.hpp"
#include "engine/ui/UiTextEdit.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <vector>

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
        ui.beginFrame(viewport, UiPointerInput {});
        CHECK(view.drawPlaytest(ui, layout, false) == MakerCommand::None);
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
    constexpr Vec2 viewport { 640.0f, 600.0f };
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
    constexpr Vec2 logicalViewport { 640.0f, 600.0f };
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

std::vector<Vec2> visibleTileSwatches(const UiContext& ui, const MakerLayout& layout)
{
    std::vector<Vec2> result;
    for (const auto& command : ui.drawData().commands) {
        if (command.kind != UiDrawKind::Solid ||
            command.rect.size != Vec2 { 16.0f, 16.0f } * layout.scale) {
            continue;
        }
        const Vec2 center = command.rect.position + command.rect.size * 0.5f;
        if (ui.contains(layout.palette, center) && command.clip && ui.contains(*command.clip, center)) {
            result.push_back(center);
        }
    }
    return result;
}

void testPlaytestControls(const FontAtlas& font)
{
    TEST("makerTestPlayAndReturnPreservePaletteSearch");
    constexpr Vec2 viewport { 1280.0f, 960.0f };
    constexpr float scale = 2.0f;
    const MakerLayout layout = MakerView::layout(viewport, scale);
    UiContext ui(font);
    MakerView view;
    const auto drawEditor = [&](UiPointerInput pointer = {}) {
        ui.beginFrame(viewport, pointer);
        const auto action = view.draw(ui, layout, {});
        ui.endFrame();
        return action.command;
    };
    const auto drawTest = [&](UiPointerInput pointer = {}, bool solved = false) {
        ui.beginFrame(viewport, pointer);
        const auto action = view.drawPlaytest(ui, layout, solved);
        ui.endFrame();
        return action;
    };
    const Vec2 testPlay { viewport.x - 166.0f * scale, 31.0f * scale };
    const Vec2 returnToMaker { viewport.x - 100.0f * scale, 31.0f * scale };
    const Vec2 board = layout.board.position + layout.board.size * 0.5f;

    CHECK(drawEditor() == MakerCommand::None);
    CHECK(view.focusTileSearch());
    auto* edit = view.activeTextEdit();
    CHECK(edit != nullptr);
    if (!edit) return;
    CHECK(edit->insert("terrain cliff"));
    CHECK(drawEditor({ .position = testPlay, .down = true, .pressedAt = testPlay }) == MakerCommand::None);
    CHECK(drawEditor({ .position = board, .releasedAt = board }) == MakerCommand::None);
    CHECK(drawEditor({ .position = testPlay, .pressedAt = testPlay, .releasedAt = testPlay }) ==
        MakerCommand::TestPlay);

    CHECK(drawTest() == MakerCommand::None);
    CHECK(!view.tileSearchFocused() && !view.focusTileSearch());
    CHECK(visibleTileSwatches(ui, layout).empty());
    CHECK(drawTest({ .position = board, .down = true, .pressedAt = board }) == MakerCommand::None);
    CHECK(drawTest({ .position = returnToMaker, .releasedAt = returnToMaker }) == MakerCommand::None);
    CHECK(drawTest({ .position = returnToMaker, .pressedAt = returnToMaker, .releasedAt = returnToMaker }, true) ==
        MakerCommand::ReturnToMaker);

    CHECK(drawEditor() == MakerCommand::None);
    CHECK(view.focusTileSearch());
    CHECK(edit->text() == "terrain cliff");
    CHECK(visibleTileSwatches(ui, layout).size() == 1);
}

void testPaletteSearchFiltersWithinGroups(const FontAtlas& font)
{
    TEST("makerPaletteSearchFiltersWithinGroups");
    constexpr Vec2 viewport { 1280.0f, 900.0f };
    const MakerLayout layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    const auto draw = [&](UiPointerInput pointer = {}, MakerViewInput input = {}) {
        ui.beginFrame(viewport, pointer);
        const auto action = view.draw(ui, layout, {}, input);
        ui.endFrame();
        return action;
    };

    (void)draw();
    const auto unfiltered = visibleTileSwatches(ui, layout);
    CHECK(view.focusTileSearch());
    auto* edit = view.activeTextEdit();
    CHECK(edit != nullptr);
    if (!edit) return;
    CHECK(edit->insert("Wa"));
    (void)draw();
    const auto prefix = visibleTileSwatches(ui, layout);
    CHECK(prefix.size() > 2);

    edit->setComposition("Ll", 2, 0);
    (void)draw();
    CHECK(visibleTileSwatches(ui, layout) == prefix);
    CHECK(edit->insert("Ll"));
    (void)draw();
    const auto walls = visibleTileSwatches(ui, layout);
    CHECK(walls.size() == 2);
    if (walls.size() != 2) return;

    const auto headingGlyph = [&](const UiDrawCommand& command) {
        return command.kind == UiDrawKind::FontGlyph && command.fontSize == 15.0f * layout.scale &&
            command.clip && intersects(*command.clip, layout.palette) &&
            command.clip->position.y < walls[0].y;
    };
    // The first match still belongs to Basics, the second to Terrain. No
    // heading is emitted for groups without a matching tile.
    CHECK(std::ranges::count_if(ui.drawData().commands, [&](const auto& command) {
        return headingGlyph(command) && command.rect.position.y < walls[0].y;
    }) == 6);
    CHECK(std::ranges::count_if(ui.drawData().commands, [&](const auto& command) {
        return headingGlyph(command) && command.rect.position.y >= walls[0].y &&
            command.rect.position.y < walls[1].y;
    }) == 7);
    CHECK(std::ranges::count_if(ui.drawData().commands, headingGlyph) == 13);

    const auto activatedWhileEditing = draw({}, { .navigation = { .activate = true } });
    CHECK(!activatedWhileEditing.tile && activatedWhileEditing.command == MakerCommand::None);
    view.blurTileSearch();
    CHECK(draw({ .position = walls[0], .pressedAt = walls[0], .releasedAt = walls[0] }).tile ==
        TileType::Wall);
    CHECK(draw({ .position = walls[1], .pressedAt = walls[1], .releasedAt = walls[1] }).tile ==
        TileType::CliffWall);

    CHECK(view.focusTileSearch());
    edit->setText("sTrEaM waTER");
    (void)draw();
    const auto stream = visibleTileSwatches(ui, layout);
    CHECK(stream.size() == 1);
    if (stream.size() == 1) {
        view.blurTileSearch();
        CHECK(draw({ .position = stream[0], .pressedAt = stream[0], .releasedAt = stream[0] }).tile ==
            TileType::StreamUp);
    }

    CHECK(view.focusTileSearch());
    edit->setText("boulder skipping stone");
    (void)draw();
    const auto skippingStone = visibleTileSwatches(ui, layout);
    CHECK(skippingStone.size() == 1);
    if (skippingStone.size() == 1) {
        view.blurTileSearch();
        CHECK(draw({ .position = skippingStone[0], .pressedAt = skippingStone[0],
            .releasedAt = skippingStone[0] }).tile == TileType::RoundRock);
    }

    CHECK(view.focusTileSearch());
    edit->setText("zzzz");
    (void)draw();
    CHECK(visibleTileSwatches(ui, layout).empty());
    const auto caret = view.textCaret();
    CHECK(std::ranges::any_of(ui.drawData().commands, [&](const UiDrawCommand& command) {
        return command.kind == UiDrawKind::FontGlyph && command.rect.position.y > caret.position.y + caret.size.y &&
            command.clip && intersects(*command.clip, layout.palette);
    }));
    edit->setText("");
    (void)draw();
    CHECK(visibleTileSwatches(ui, layout) == unfiltered);
}

void testPaletteSelectsDistinctObjectsAndRotators(const FontAtlas& font)
{
    TEST("makerPaletteSelectsDistinctObjectsAndRotators");
    constexpr Vec2 viewport { 640.0f, 600.0f };
    const MakerLayout layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    const auto draw = [&](UiPointerInput pointer = {}) {
        ui.beginFrame(viewport, pointer);
        const auto action = view.draw(ui, layout, {});
        ui.endFrame();
        return action;
    };
    (void)draw();
    const auto searchAndSelect = [&](std::string_view query, std::span<const TileType> expected) {
        CHECK(view.focusTileSearch());
        auto* edit = view.activeTextEdit();
        CHECK(edit != nullptr);
        if (!edit) return;
        edit->setText(query);
        (void)draw();
        const auto rows = visibleTileSwatches(ui, layout);
        CHECK(rows.size() == expected.size());
        if (rows.size() != expected.size()) return;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const Vec2 point = rows[i];
            CHECK(draw({ .position = point, .pressedAt = point, .releasedAt = point }).tile == expected[i]);
        }
    };
    const std::array crystals { TileType::Crystal, TileType::SkippingCrystal };
    const std::array rotators { TileType::RotatorClockwise, TileType::RotatorCounterClockwise };
    searchAndSelect("objects crystal", crystals);
    searchAndSelect("mechanisms rotator", rotators);
    searchAndSelect("counter-clockwise", std::span(rotators).last(1));
}

void testPaletteThumbnailsAndBoardGuide(const FontAtlas& font)
{
    TEST("makerPaletteDrawsNativeThumbnailsAndInspectsBoardTile");
    constexpr Vec2 viewport { 640.0f, 480.0f };
    const MakerLayout layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    const auto textureFor = [](TileType tile) {
        return RenderTexture { static_cast<uint32_t>(tile) + 1 };
    };
    std::vector<TileType> requested;
    MakerViewInput input;
    input.tileThumbnail = [&](TileType tile) {
        requested.push_back(tile);
        return tile == TileType::Wall ? noTexture : textureFor(tile);
    };
    const auto draw = [&](Vec2 position, const MakerViewState& state = {}) {
        requested.clear();
        ui.beginFrame(viewport, UiPointerInput { .position = position });
        (void)view.draw(ui, layout, state, input);
        ui.endFrame();
    };
    const auto thumbnail = [&](TileType tile, float size) {
        return std::ranges::find_if(ui.drawData().commands, [&](const auto& command) {
            return command.kind == UiDrawKind::TextureImage && command.texture == textureFor(tile) &&
                command.rect.size == Vec2 { size, size };
        });
    };

    draw({});
    CHECK(requested.size() < 10);
    CHECK(std::ranges::find(requested, TileType::Minecart) == requested.end());
    CHECK(!requested.empty() && requested.back() == TileType::Ground);
    CHECK(visibleTileSwatches(ui, layout).size() == 1);
    const auto ground = thumbnail(TileType::Ground, 28.0f);
    CHECK(ground != ui.drawData().commands.end());
    if (ground == ui.drawData().commands.end()) return;
    CHECK(ground->clip && intersects(*ground->clip, layout.palette));

    const auto wallSwatch = visibleTileSwatches(ui, layout).front();
    draw(wallSwatch);
    CHECK(!requested.empty() && requested.back() == TileType::Wall);

    const Vec2 board = layout.board.position + layout.board.size * 0.5f;
    const MakerViewState blockedPlacement {
        .brush = TileType::Water,
        .hovered = GridPosition3 { 1, 2, 0 },
        .placementHint = "Lily pads must be placed in an empty cell directly above water.",
        .placementBlocked = true,
        .boardTile = TileType::PortalEast,
    };
    draw(board, blockedPlacement);
    CHECK(!requested.empty() && requested.back() == TileType::PortalEast);
    const auto inspector = thumbnail(TileType::PortalEast, 30.0f);
    CHECK(inspector != ui.drawData().commands.end());
    if (inspector != ui.drawData().commands.end()) {
        CHECK(inspector->clip && intersects(*inspector->clip, layout.palette));
        CHECK(std::ranges::any_of(ui.drawData().commands, [&](const auto& command) {
            return command.kind == UiDrawKind::FontGlyph && command.clip &&
                intersects(*command.clip, layout.palette) &&
                command.rect.position.y > inspector->rect.position.y + inspector->rect.size.y;
        }));
    }
    const auto warningText = [&](const UiDrawCommand& command) {
        return command.kind == UiDrawKind::FontGlyph && command.clip &&
            intersects(*command.clip, layout.board) && command.color == Vec4 { 1.0f, 0.66f, 0.57f, 1.0f };
    };
    const auto warning = std::ranges::find_if(ui.drawData().commands, warningText);
    CHECK(warning != ui.drawData().commands.end());
    if (warning == ui.drawData().commands.end()) return;
    const float firstLineY = warning->rect.position.y;
    CHECK(firstLineY > board.y);
    CHECK(std::ranges::any_of(ui.drawData().commands, [&](const auto& command) {
        return warningText(command) && command.rect.position.y > firstLineY + 15.0f;
    }));
    draw({ board.x, firstLineY + 5.0f }, blockedPlacement);
    const auto movedWarning = std::ranges::find_if(ui.drawData().commands, warningText);
    CHECK(movedWarning != ui.drawData().commands.end());
    if (movedWarning != ui.drawData().commands.end()) {
        CHECK(movedWarning->rect.position.y < board.y);
    }
}

void testPaletteKeyboardRevealsRows(const FontAtlas& font)
{
    TEST("makerPaletteKeyboardRevealsOffscreenTiles");
    constexpr Vec2 viewport { 640.0f, 480.0f };
    const MakerLayout layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    std::optional<TileType> inspected;
    MakerViewInput input;
    input.navigation.next = true;
    input.tileThumbnail = [&](TileType tile) {
        inspected = tile;
        return noTexture;
    };
    std::vector<TileType> visited;
    for (int frame = 0; frame < 64; ++frame) {
        ui.beginFrame(viewport, UiPointerInput {});
        (void)view.draw(ui, layout, {}, input);
        ui.endFrame();
        if (inspected && std::ranges::find(visited, *inspected) == visited.end()) {
            visited.push_back(*inspected);
        }
    }
    CHECK(std::ranges::find(visited, TileType::LilyPad) != visited.end());
    CHECK(std::ranges::find(visited, TileType::MinecartGate) != visited.end());
}

void testPaletteSearchPressOwnsGesture(const FontAtlas& font)
{
    TEST("makerPaletteSearchPressOwnsGesture");
    constexpr Vec2 viewport { 640.0f, 480.0f };
    const MakerLayout layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    const auto draw = [&](UiPointerInput pointer = {}) {
        ui.beginFrame(viewport, pointer);
        const auto action = view.draw(ui, layout, {});
        CHECK(!action.tile && action.command == MakerCommand::None);
        CHECK(!ui.dragPosition("maker.board", layout.board));
        ui.endFrame();
    };

    (void)draw();
    CHECK(view.focusTileSearch());
    (void)draw();
    const auto caret = view.textCaret();
    CHECK(caret.size.y > 0.0f);
    const Vec2 press = caret.position + Vec2 { 3.0f, 3.0f };
    view.blurTileSearch();
    CHECK(!view.tileSearchFocused() && !view.activeTextEdit());
    CHECK(view.handleTileSearchPress(ui, press, false));
    auto* edit = view.activeTextEdit();
    CHECK(edit != nullptr);
    if (!edit) return;

    CHECK(edit->insert("Pa"));
    const auto typedCaret = edit->caret();
    draw({ .position = press, .down = true, .pressedAt = press });
    CHECK(edit->caret() == typedCaret);
    const Vec2 board = layout.board.position + layout.board.size * 0.5f;
    draw({ .position = board, .releasedAt = board });
    CHECK(view.tileSearchFocused());
    CHECK(view.handleTileSearchPress(ui, board, false));
    CHECK(!view.tileSearchFocused() && !view.activeTextEdit());
    CHECK(edit->text() == "Pa");
}

void testDraftSaveDialog(const FontAtlas& font)
{
    TEST("makerSaveDialogUsesNativeTextAndOwnsInput");
    using Command = MakerDraftDialogAction::Command;
    constexpr Vec2 viewport { 640.0f, 480.0f };
    constexpr Vec2 toolbarSave { 300.0f, 31.0f };
    constexpr Vec2 toolbarLoad { 374.0f, 31.0f };
    constexpr Vec2 dialogSave { 500.0f, 318.0f };
    const MakerLayout layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    const auto click = [](Vec2 at) {
        return UiPointerInput { .position = at, .pressedAt = at, .releasedAt = at };
    };
    const auto editor = [&](Vec2 at) {
        ui.beginFrame(viewport, click(at));
        const auto action = view.draw(ui, layout, {});
        ui.endFrame();
        return action.command;
    };
    const auto dialog = [&](UiPointerInput pointer = {}, MakerViewInput input = {}) {
        ui.beginFrame(viewport, pointer);
        const auto editorAction = view.draw(ui, layout, {}, { .interactive = false });
        CHECK(editorAction.command == MakerCommand::None && !editorAction.tile);
        const auto action = view.drawDraftDialog(ui, viewport, layout.scale, {}, input);
        ui.endFrame();
        return action;
    };
    CHECK(editor(toolbarSave) == MakerCommand::Save);
    CHECK(editor(toolbarLoad) == MakerCommand::Load);

    view.beginDraftDialog(MakerDraftDialogMode::Save);
    auto* edit = view.activeDraftNameEdit();
    CHECK(view.draftNameFocused() && edit);
    if (!edit) return;
    CHECK(dialog({}, { .navigation = { .activate = true } }).command == Command::None);
    CHECK(edit->insert("Oak"));
    const auto activated = dialog({}, { .navigation = { .activate = true } });
    CHECK(activated.command == Command::Submit && activated.name == "Oak");
    CHECK(dialog(click(toolbarLoad)).command == Command::None);
    CHECK(!view.draftNameFocused());

    view.beginDraftDialog(MakerDraftDialogMode::Save, "Oak");
    CHECK(edit->selectedText() == "Oak");
    CHECK(edit->insert("Oak and Ivy"));
    edit->setComposition("!", 1, 0);
    CHECK(view.submitDraftName().command == Command::None);
    edit->clearComposition();
    CHECK(view.submitDraftName().name == "Oak and Ivy");
    CHECK(dialog().command == Command::None);
    const auto caret = view.draftNameCaret();
    CHECK(caret.size.y > 0.0f);
    view.blurDraftName();
    CHECK(view.handleDraftNamePress(ui, caret.position + Vec2 { 0.0f, 1.0f }, false));
    CHECK(view.activeDraftNameEdit() == edit);
    const auto clicked = dialog(click(dialogSave));
    CHECK(clicked.command == Command::Submit && clicked.name == "Oak and Ivy");
}

void testDraftLoadDialog(const FontAtlas& font)
{
    TEST("makerLoadDialogScrollsAndConfirmsByName");
    using Command = MakerDraftDialogAction::Command;
    constexpr Vec2 viewport { 640.0f, 480.0f };
    std::vector<std::string> drafts;
    for (int i = 0; i < 18; ++i) drafts.push_back("Draft " + std::to_string(i + 1));
    UiContext ui(font);
    MakerView view;
    const auto draw = [&](const MakerDraftDialogState& state, UiPointerInput pointer = {},
                          MakerViewInput input = {}) {
        ui.beginFrame(viewport, pointer);
        const auto action = view.drawDraftDialog(ui, viewport, 1.0f, state, input);
        ui.endFrame();
        return action;
    };
    view.beginDraftDialog(MakerDraftDialogMode::Load);
    CHECK(!view.draftNameFocused());
    const MakerDraftDialogState list { .mode = MakerDraftDialogMode::Load, .names = drafts };
    CHECK(draw(list).command == Command::None);
    constexpr Vec2 secondRow { 110.0f, 148.0f };
    constexpr Vec2 loadButton { 500.0f, 419.0f };
    CHECK(draw(list, { .position = secondRow, .pressedAt = secondRow, .releasedAt = secondRow }).command ==
        Command::None);
    const auto load = draw(list, { .position = loadButton, .pressedAt = loadButton, .releasedAt = loadButton });
    CHECK(load.command == Command::Submit && load.name == drafts[1]);

    view.beginDraftDialog(MakerDraftDialogMode::Load);
    for (int i = 0; i < 13; ++i) {
        CHECK(draw(list, {}, { .navigation = { .next = true } }).command == Command::None);
    }
    const auto fromScroll = draw(list, {}, { .navigation = { .activate = true } });
    CHECK(fromScroll.command == Command::Submit && fromScroll.name == drafts[13]);

    for (const auto mode : { MakerDraftDialogMode::Overwrite, MakerDraftDialogMode::Discard }) {
        view.beginDraftDialog(mode);
        const MakerDraftDialogState confirmation { .mode = mode, .pendingName = drafts[13] };
        CHECK(draw(confirmation, {}, { .navigation = { .activate = true } }).command == Command::Cancel);
        CHECK(draw(confirmation, {}, { .navigation = { .next = true } }).command == Command::None);
        const auto confirm = draw(confirmation, {}, { .navigation = { .activate = true } });
        CHECK(confirm.command == Command::Submit && confirm.name == drafts[13]);
    }
}

void testNewDraftDialog(const FontAtlas& font)
{
    TEST("makerNewDraftSelectsBoardSizeAndSafelyConfirms");
    using Command = MakerDraftDialogAction::Command;
    constexpr Vec2 viewport { 640.0f, 480.0f };
    const MakerLayout layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    const auto click = [](Vec2 position) {
        return UiPointerInput { .position = position, .pressedAt = position, .releasedAt = position };
    };
    constexpr Vec2 toolbarNewDraft { 214.0f, 31.0f };
    ui.beginFrame(viewport, click(toolbarNewDraft));
    CHECK(view.draw(ui, layout, {}).command == MakerCommand::NewDraft);
    ui.endFrame();

    const auto draw = [&](MakerDraftDialogMode mode, UiPointerInput pointer = {}, UiNavigation navigation = {}) {
        ui.beginFrame(viewport, pointer);
        const auto editor = view.draw(ui, layout, {}, { .interactive = false });
        CHECK(editor.command == MakerCommand::None && !editor.tile);
        const auto action = view.drawDraftDialog(ui, viewport, layout.scale, { .mode = mode }, { .navigation = navigation });
        ui.endFrame();
        return action;
    };
    constexpr auto picker = MakerDraftDialogMode::NewDraft;
    constexpr auto discard = MakerDraftDialogMode::DiscardNew;
    constexpr Vec2 widthUp { 537.0f, 182.0f };
    constexpr Vec2 heightDown { 453.0f, 229.0f };
    constexpr Vec2 create { 500.0f, 341.0f };
    view.beginDraftDialog(picker);
    CHECK(!view.draftNameFocused());
    CHECK(draw(picker, click(toolbarNewDraft)).command == Command::None);
    CHECK(draw(picker, click(widthUp)).command == Command::None);
    CHECK(draw(picker, click(heightDown)).command == Command::None);
    const auto requested = draw(picker, click(create));
    CHECK(requested.command == Command::Submit);
    CHECK(requested.width == MakerSession::defaultDraftWidth + 1);
    CHECK(requested.height == MakerSession::defaultDraftHeight - 1);

    view.beginDraftDialog(discard);
    CHECK(draw(discard, {}, { .activate = true }).command == Command::Cancel);
    view.resumeBoardSizeDialog();
    const auto resumed = draw(picker, {}, { .activate = true });
    CHECK(resumed.command == Command::Submit);
    CHECK(resumed.width == requested.width && resumed.height == requested.height);

    view.beginDraftDialog(discard);
    CHECK(draw(discard, {}, { .next = true }).command == Command::None);
    const auto confirmed = draw(discard, {}, { .activate = true });
    CHECK(confirmed.command == Command::Submit);
    CHECK(confirmed.width == requested.width && confirmed.height == requested.height);

    view.beginDraftDialog(picker);
    CHECK(draw(picker, {}, { .next = true, .activate = true }).command == Command::None);
    CHECK(draw(picker, {}, { .next = true }).command == Command::None);
    CHECK(draw(picker, {}, { .next = true }).command == Command::None);
    CHECK(draw(picker, {}, { .next = true, .activate = true }).command == Command::None);
    const auto keyboard = draw(picker, click(create));
    CHECK(keyboard.command == Command::Submit);
    CHECK(keyboard.width == MakerSession::defaultDraftWidth - 1);
    CHECK(keyboard.height == MakerSession::defaultDraftHeight + 1);

    constexpr Vec2 widthSliderEnd { 414.0f, 181.0f };
    CHECK(draw(picker, click(widthSliderEnd)).command == Command::None);
    CHECK(draw(picker, click(widthUp)).command == Command::None);
    const auto maximum = draw(picker, click(create));
    CHECK(maximum.command == Command::Submit);
    CHECK(maximum.width == MakerSession::maxDraftDimension);
}

void testResizeBoardDialog(const FontAtlas& font)
{
    TEST("makerResizeBoardKeepsSizeThroughCropConfirmation");
    using Command = MakerDraftDialogAction::Command;
    constexpr auto resize = MakerDraftDialogMode::ResizeBoard;
    constexpr auto crop = MakerDraftDialogMode::ConfirmCrop;
    constexpr Vec2 viewport { 640.0f, 480.0f };
    const MakerLayout layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    const auto click = [](Vec2 position) {
        return UiPointerInput { .position = position, .pressedAt = position, .releasedAt = position };
    };
    constexpr Vec2 compactResize { 568.0f, 69.0f };
    ui.beginFrame(viewport, click(compactResize));
    CHECK(view.draw(ui, layout, {}).command == MakerCommand::ResizeBoard);
    ui.endFrame();

    constexpr Vec2 wideViewport { 1440.0f, 900.0f };
    ui.beginFrame(wideViewport, click({ 881.0f, 31.0f }));
    CHECK(view.draw(ui, MakerView::layout(wideViewport), {}).command == MakerCommand::ResizeBoard);
    ui.endFrame();

    const auto draw = [&](MakerDraftDialogMode mode, UiPointerInput pointer = {}, UiNavigation navigation = {}) {
        ui.beginFrame(viewport, pointer);
        const auto editor = view.draw(ui, layout, {}, { .interactive = false });
        CHECK(editor.command == MakerCommand::None && !editor.tile);
        const auto action = view.drawDraftDialog(ui, viewport, layout.scale,
            { .mode = mode, .removedTileCount = 9, .resizePreviewPanel = layout.palette },
            { .navigation = navigation });
        ui.endFrame();
        CHECK(std::ranges::none_of(ui.drawData().commands, [&](const UiDrawCommand& command) {
            return intersects(command.rect, layout.board) &&
                (!command.clip || intersects(*command.clip, layout.board));
        }));
        return action;
    };
    view.beginResizeBoardDialog(70, 12);
    CHECK(!view.draftNameFocused());
    CHECK(view.resizePreviewSize() == (GridPosition { 70, 12 }));
    const auto initial = draw(resize, {}, { .activate = true });
    CHECK(initial.command == Command::Submit);
    CHECK(initial.width == 70 && initial.height == 12);
    CHECK(draw(resize, click({ 186.0f, 186.0f })).command == Command::None);
    CHECK(view.resizePreviewSize() == (GridPosition { 70, 12 }));
    CHECK(draw(resize, click({ 110.0f, 186.0f })).command == Command::None);
    CHECK(view.resizePreviewSize() == (GridPosition { 69, 12 }));
    CHECK(draw(resize, click({ 186.0f, 248.0f })).command == Command::None);
    CHECK(view.resizePreviewSize() == (GridPosition { 69, 13 }));
    const auto requested = draw(resize, click({ 154.0f, 393.0f }));
    CHECK(requested.command == Command::Submit);
    CHECK(requested.width == 69 && requested.height == 13);

    view.beginDraftDialog(crop);
    CHECK(view.resizePreviewSize() == (GridPosition { 69, 13 }));
    CHECK(draw(crop, {}, { .activate = true }).command == Command::Cancel);
    view.resumeBoardSizeDialog();
    const auto resumed = draw(resize, {}, { .activate = true });
    CHECK(resumed.command == Command::Submit);
    CHECK(resumed.width == requested.width && resumed.height == requested.height);

    view.beginDraftDialog(crop);
    CHECK(draw(crop, {}, { .next = true }).command == Command::None);
    const auto confirmed = draw(crop, {}, { .activate = true });
    CHECK(confirmed.command == Command::Submit);
    CHECK(confirmed.width == requested.width && confirmed.height == requested.height);

    view.beginResizeBoardDialog(10, 8);
    const Vec2 widthSlider { 113.0f, 215.0f };
    CHECK(draw(resize, { .position = widthSlider, .down = true, .pressedAt = widthSlider }).command == Command::None);
    CHECK(view.resizePreviewSize().x > 10);
    const Vec2 sliderEnd { 250.0f, 215.0f };
    CHECK(draw(resize, { .position = sliderEnd, .releasedAt = sliderEnd }).command == Command::None);
    CHECK(view.resizePreviewSize() == (GridPosition { static_cast<int>(MakerSession::maxDraftDimension), 8 }));
}

void testLinkInspectorNativeControls(const FontAtlas& font)
{
    TEST("makerLinkInspectorNativeControls");
    constexpr Vec2 viewport { 640.0f, 480.0f };
    const MakerLayout layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    const auto click = [](Vec2 position) {
        return UiPointerInput { .position = position, .pressedAt = position, .releasedAt = position };
    };
    ui.beginFrame(viewport, click({ 169.0f, 69.0f }));
    CHECK(view.draw(ui, layout, {}).command == MakerCommand::Connect);
    ui.endFrame();
    CHECK(std::ranges::count_if(ui.drawData().commands, [&](const UiDrawCommand& command) {
        return command.kind == UiDrawKind::FontGlyph && command.clip &&
            ui.contains(*command.clip, { 169.0f, 69.0f });
    }) == 7);

    const std::array groups { std::string("Entrance"), std::string("Exit") };
    const std::array members { std::string("Lever"), std::string("Gate") };
    const UiRect scene { layout.board.position + Vec2 { 16.0f, 14.0f },
        layout.board.size - Vec2 { 32.0f, 28.0f } };
    constexpr Vec4 linkColor { 0.25f, 0.85f, 0.93f, 1.0f };
    const Vec2 from = scene.position + Vec2 { 5.0f, 20.0f };
    const Vec2 to = scene.position + Vec2 { 150.0f, 105.0f };
    const std::array lines { MakerLinkLine { from, to, linkColor, "Entrance" } };
    const std::array markers { MakerLinkMarker { from, linkColor, true }, MakerLinkMarker { to, linkColor } };
    const MakerViewState state {
        .linkInspector = MakerLinkInspector {
            .kind = MakerLinkInspector::Kind::Device,
            .title = "Lever",
            .detail = "Connect to a gate.",
            .members = members,
            .groups = groups,
            .activeGroup = 0,
            .canDisconnect = true,
            .canClear = true,
            .canRename = true,
            .canCreate = true,
        },
        .linkLines = lines,
        .linkMarkers = markers,
        .linkViewport = scene,
    };
    const auto draw = [&](UiPointerInput pointer = {}, bool interactive = true) {
        ui.beginFrame(viewport, pointer);
        const auto action = view.draw(ui, layout, state, { .interactive = interactive });
        ui.endFrame();
        return action;
    };
    CHECK(draw().command == MakerCommand::None);
    CHECK(!view.focusTileSearch());
    const auto visibleLines = [&] {
        return std::ranges::count_if(ui.drawData().commands, [&](const UiDrawCommand& command) {
            return command.kind == UiDrawKind::Solid && command.color == linkColor &&
                command.clip == scene;
        });
    };
    CHECK(visibleLines() > 2);
    CHECK(!draw(click(to)).tile);
    CHECK(draw(click({ 56.0f, 211.0f })).command == MakerCommand::NewLinkGroup);
    CHECK(draw(click({ 153.0f, 211.0f })).command == MakerCommand::RenameLinkGroup);
    CHECK(draw(click({ 56.0f, 247.0f })).command == MakerCommand::Disconnect);
    CHECK(draw(click({ 153.0f, 247.0f })).command == MakerCommand::ClearLinkSelection);
    CHECK(draw(click({ 70.0f, 323.0f })).command == MakerCommand::None);
    const auto assign = draw(click({ 70.0f, 352.0f }));
    CHECK(assign.command == MakerCommand::AssignLinkGroup && assign.linkGroup == 1);
    CHECK(draw(click({ 56.0f, 211.0f }), false).command == MakerCommand::None);
    CHECK(visibleLines() == 0);

    std::vector<MakerLinkLine> crowdedLines(200, lines.front());
    std::vector<MakerLinkMarker> crowdedMarkers(400, markers.front());
    MakerViewState crowded = state;
    crowded.linkLines = crowdedLines;
    crowded.linkMarkers = crowdedMarkers;
    ui.beginFrame(viewport, UiPointerInput {});
    (void)view.draw(ui, layout, crowded);
    ui.endFrame();
    CHECK(ui.droppedCommands() == 0);

    view.beginDraftDialog(MakerDraftDialogMode::RenameLinkGroup, "Entrance");
    CHECK(view.draftNameFocused());
    auto* edit = view.activeDraftNameEdit();
    CHECK(edit != nullptr);
    if (!edit) return;
    CHECK(edit->selectedText() == "Entrance");
    CHECK(edit->insert("Front door"));
    CHECK(view.submitDraftName().name == "Front door");
    view.beginDraftDialog(MakerDraftDialogMode::NameLinkGroup);
    CHECK(view.draftNameFocused());
    CHECK(view.submitDraftName().command == MakerDraftDialogAction::Command::None);
}

void testLinkLayerAndElevatorControls(const FontAtlas& font)
{
    TEST("makerLinkLayerAndElevatorControls");
    constexpr Vec2 viewport { 640.0f, 480.0f };
    const MakerLayout layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    MakerViewState state;
    const auto click = [&](Vec2 position) {
        ui.beginFrame(viewport, UiPointerInput { .position = position, .pressedAt = position, .releasedAt = position });
        const auto command = view.draw(ui, layout, state).command;
        ui.endFrame();
        return command;
    };
    constexpr Vec2 layerButton { 212.0f, 69.0f };
    CHECK(click(layerButton) == MakerCommand::Rotate);
    state.linkInspector = MakerLinkInspector {
        .kind = MakerLinkInspector::Kind::Device,
        .title = "Elevator",
        .detail = "Connect to a lever.",
    };
    CHECK(click(layerButton) == MakerCommand::None);
    state.layerCount = 2;
    CHECK(click(layerButton) == MakerCommand::ToggleLinkLayer);
    state.linkLayerIsolated = true;
    CHECK(click(layerButton) == MakerCommand::ToggleLinkLayer);

    constexpr Vec2 stopButton { 56.0f, 211.0f };
    CHECK(click(stopButton) == MakerCommand::None);
    state.linkInspector->elevatorStopOnLayer = false;
    CHECK(click(stopButton) == MakerCommand::ToggleElevatorStop);
    state.linkInspector->elevatorStopOnLayer = true;
    CHECK(click(stopButton) == MakerCommand::ToggleElevatorStop);
}

} // namespace

int main()
{
    const auto font = sokoban::FontAtlas::loadDefault(
        std::filesystem::path(SOKOBAN_TEST_ASSET_DIR));
    testBoardStaysOpen(font);
    testPalettePointerOwnership(font);
    testDisplayScale(font);
    testPlaytestControls(font);
    testPaletteSearchFiltersWithinGroups(font);
    testPaletteSelectsDistinctObjectsAndRotators(font);
    testPaletteThumbnailsAndBoardGuide(font);
    testPaletteKeyboardRevealsRows(font);
    testPaletteSearchPressOwnsGesture(font);
    testDraftSaveDialog(font);
    testDraftLoadDialog(font);
    testNewDraftDialog(font);
    testResizeBoardDialog(font);
    testLinkInspectorNativeControls(font);
    testLinkLayerAndElevatorControls(font);
    std::cout << "MakerViewTests: " << checks - failures << " of " << checks
              << " checks passed\n";
    return failures != 0 ? 1 : 0;
}
