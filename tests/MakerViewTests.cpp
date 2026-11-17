#include "TestHarness.hpp"

#include "engine/InputBindings.hpp"
#include "engine/MakerSession.hpp"
#include "engine/ui/FontAtlas.hpp"
#include "engine/ui/UiDialog.hpp"
#include "engine/ui/MakerTileGuide.hpp"
#include "engine/ui/MakerView.hpp"
#include "engine/ui/UiColorValues.hpp"
#include "engine/ui/UiTextEdit.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <utility>
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

bool leavesBoardClear(const UiDrawCommand& command, const MakerLayout& layout)
{
    if (!intersects(command.rect, layout.board) || (command.clip && !intersects(*command.clip, layout.board))) {
        return true;
    }
    const UiRect controls {
        layout.board.position + Vec2 { 10.0f, 10.0f } * layout.scale,
        Vec2 { 350.0f, 28.0f } * layout.scale,
    };
    if (!command.clip) return false;
    const UiRect clip = *command.clip;
    return clip.position.x >= controls.position.x && clip.position.y >= controls.position.y &&
        clip.position.x + clip.size.x <= controls.position.x + controls.size.x &&
        clip.position.y + clip.size.y <= controls.position.y + controls.size.y;
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
        CHECK(std::ranges::all_of(ui.drawData().commands, [&](const UiDrawCommand& command) {
            return leavesBoardClear(command, layout);
        }));
        const MakerLayout playtest = MakerView::playtestLayout(viewport);
        CHECK(playtest.toolbar == (UiRect { {}, { viewport.x, 62.0f } }));
        CHECK(playtest.board == (UiRect { { 0.0f, 62.0f }, { viewport.x, viewport.y - 62.0f } }));
        CHECK(playtest.palette.size == Vec2 {});
        CHECK(playtest.footer.size == Vec2 {});
        std::vector<std::pair<UiRect, UiRect>> activeGlyphs;
        for (bool solved : { false, true }) {
            ui.beginFrame(viewport, UiPointerInput {});
            CHECK(view.drawPlaytest(ui, playtest, solved) == MakerCommand::None);
            ui.endFrame();
            std::vector<std::pair<UiRect, UiRect>> glyphs;
            for (const auto& command : ui.drawData().commands) {
                CHECK(!intersects(command.rect, playtest.board) ||
                    (command.clip && !intersects(*command.clip, playtest.board)));
                if (command.kind == UiDrawKind::FontGlyph) {
                    glyphs.emplace_back(command.rect, command.uvRect);
                }
            }
            CHECK(!glyphs.empty());
            if (solved) {
                CHECK(glyphs != activeGlyphs);
            } else {
                activeGlyphs = std::move(glyphs);
            }
        }
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
    const MakerLayout logicalPlaytest = MakerView::playtestLayout(logicalViewport);
    const MakerLayout physicalPlaytest = MakerView::playtestLayout(viewport, scale);
    CHECK(physicalPlaytest.toolbar == pixels(logicalPlaytest.toolbar));
    CHECK(physicalPlaytest.board == pixels(logicalPlaytest.board));
    CHECK(physicalPlaytest.scale == scale);

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
    CHECK(std::ranges::all_of(ui.drawData().commands, [&](const UiDrawCommand& command) {
        return leavesBoardClear(command, physical);
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
    const MakerLayout playtest = MakerView::playtestLayout(viewport, scale);
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
        const auto action = view.drawPlaytest(ui, playtest, solved);
        ui.endFrame();
        return action;
    };
    const Vec2 testPlay { viewport.x - 206.0f * scale, 31.0f * scale };
    const Vec2 returnToMaker { viewport.x - 100.0f * scale, 31.0f * scale };
    const Vec2 board = layout.board.position + layout.board.size * 0.5f;
    const Vec2 playtestBoard = playtest.board.position + playtest.board.size * 0.5f;

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
    CHECK(drawTest({ .position = playtestBoard, .down = true, .pressedAt = playtestBoard }) == MakerCommand::None);
    CHECK(drawTest({ .position = returnToMaker, .releasedAt = returnToMaker }) == MakerCommand::None);
    CHECK(drawTest({ .position = returnToMaker, .pressedAt = returnToMaker, .releasedAt = returnToMaker }, true) ==
        MakerCommand::ReturnToMaker);

    CHECK(drawEditor() == MakerCommand::None);
    CHECK(view.focusTileSearch());
    CHECK(edit->text() == "terrain cliff");
    CHECK(visibleTileSwatches(ui, layout).size() == 1);
}

void testHelpDialog(const FontAtlas& font)
{
    TEST("makerHelpOpensScrollsAndShowsCurrentBindings");
    constexpr Vec2 viewport { 640.0f, 480.0f };
    const MakerLayout layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    const auto click = [](Vec2 position) {
        return UiPointerInput { .position = position, .pressedAt = position, .releasedAt = position };
    };
    constexpr Vec2 helpButton { viewport.x - 29.0f, 31.0f };
    constexpr Vec2 backButton { viewport.x - 96.0f, 31.0f };
    constexpr Vec2 testPlayButton { viewport.x - 206.0f, 31.0f };
    const auto editor = [&](UiPointerInput pointer) {
        ui.beginFrame(viewport, pointer);
        const auto command = view.draw(ui, layout, {}).command;
        ui.endFrame();
        return command;
    };
    CHECK(editor({ .position = helpButton, .down = true, .pressedAt = helpButton }) == MakerCommand::None);
    CHECK(editor({ .position = backButton, .releasedAt = backButton }) == MakerCommand::None);
    CHECK(editor(click(backButton)) == MakerCommand::Back);
    CHECK(editor(click(helpButton)) == MakerCommand::Help);

    auto bindings = defaultInputBindings();
    std::vector<std::pair<UiRect, UiRect>> helpGlyphs;
    const auto dialog = [&](UiPointerInput pointer = {}, MakerViewInput input = {}) {
        ui.beginFrame(viewport, pointer);
        const auto editorAction = view.draw(ui, layout, {}, { .interactive = false });
        CHECK(editorAction.command == MakerCommand::None && !editorAction.tile);
        const std::size_t overlay = ui.drawData().commands.size();
        const bool closed = view.drawHelp(ui, viewport, layout.scale, bindings, input);
        ui.endFrame();
        helpGlyphs.clear();
        const auto& commands = ui.drawData().commands;
        for (std::size_t index = overlay; index < commands.size(); ++index) {
            const auto& command = commands[index];
            if (command.kind == UiDrawKind::FontGlyph) {
                helpGlyphs.emplace_back(command.rect, command.uvRect);
            }
        }
        return closed;
    };

    view.beginHelp();
    CHECK(!dialog());
    const auto top = helpGlyphs;
    CHECK(!top.empty());
    CHECK(!dialog(click(testPlayButton)));
    CHECK(helpGlyphs == top);
    CHECK(!dialog({}, { .wheelY = -100.0f, .wheelPosition = Vec2 {} }));
    CHECK(helpGlyphs == top);
    CHECK(!dialog({}, { .wheelY = -100.0f, .wheelPosition = viewport * 0.5f }));
    CHECK(helpGlyphs != top);
    CHECK(!helpGlyphs.empty());
    CHECK(dialog({}, { .navigation = { .activate = true } }));

    view.beginHelp();
    CHECK(!dialog());
    CHECK(helpGlyphs == top);
    assignBinding(bindings, InputAction::EditorRotateTile, KeyboardBinding { "P" });
    CHECK(!dialog());
    CHECK(helpGlyphs != top);
}

void testCameraDialog(const FontAtlas& font)
{
    TEST("makerCameraDialogPreviewsAnglesAndOwnsDetachedControls");
    using Command = MakerCameraDialogAction::Command;
    constexpr Vec2 viewport { 640.0f, 540.0f };
    const MakerLayout layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    MakerCameraDialogState state { .canDetach = true };
    const auto click = [](Vec2 position) {
        return UiPointerInput { .position = position, .pressedAt = position, .releasedAt = position };
    };
    ui.beginFrame(viewport, click({ 459.0f, 69.0f }));
    CHECK(view.draw(ui, layout, {}).command == MakerCommand::Camera);
    ui.endFrame();

    const auto draw = [&](UiPointerInput pointer = {}, UiNavigation navigation = {}) {
        ui.beginFrame(viewport, pointer);
        const auto editor = view.draw(ui, layout, { .showViewportControls = false }, { .interactive = false });
        CHECK(editor.command == MakerCommand::None);
        const auto action = view.drawCameraDialog(ui, layout, state, { .navigation = navigation });
        ui.endFrame();
        CHECK(std::ranges::none_of(ui.drawData().commands, [&](const UiDrawCommand& draw) {
            return intersects(draw.rect, layout.board) && (!draw.clip || intersects(*draw.clip, layout.board));
        }));
        return action;
    };
    const Vec2 tiltRight = layout.palette.position + Vec2 { 154.0f, 86.0f };
    const Vec2 detach = layout.palette.position + Vec2 { 160.0f, 214.0f };
    const Vec2 reset = layout.palette.position + Vec2 { 90.0f, 168.0f };
    const Vec2 attach = layout.palette.position + Vec2 { 55.0f, 214.0f };
    const Vec2 apply = layout.palette.position + Vec2 { 150.0f, 324.0f };
    view.beginCameraDialog();
    CHECK(!draw().angles);
    const auto slide = draw(click(tiltRight));
    CHECK(slide.command == Command::None);
    CHECK(slide.angles && slide.angles->valid());
    CHECK(slide.angles && slide.angles->pitchDegrees > state.defaults.pitchDegrees);
    state.angles = slide.angles;
    if (!state.angles) return;
    CHECK(draw(click(reset)).angles == std::nullopt);
    CHECK(draw(click(apply)).command == Command::Apply);
    CHECK(draw(click(detach)).command == Command::ToggleDetached);

    state.detached = true;
    const auto ignored = draw(click(tiltRight));
    CHECK(ignored.command == Command::None);
    CHECK(ignored.angles == state.angles);
    CHECK(draw(click(detach)).command == Command::ResetPose);
    CHECK(draw(click(attach)).command == Command::ToggleDetached);
    state.mouseCaptured = true;
    CHECK(draw(click(attach)).command == Command::None);
    CHECK(draw({}, { .activate = true }).command == Command::None);
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

void testPaletteGroupCollapse(const FontAtlas& font)
{
    TEST("makerPaletteGroupCollapseSeparatesNormalAndSearchState");
    constexpr Vec2 viewport { 1280.0f, 900.0f };
    const MakerLayout layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    const auto textureFor = [](TileType tile) { return RenderTexture { static_cast<uint32_t>(tile) + 1 }; };
    const auto draw = [&](UiPointerInput pointer = {}, UiNavigation navigation = {}) {
        ui.beginFrame(viewport, pointer);
        const auto action = view.draw(ui, layout, {}, { .navigation = navigation, .tileThumbnail = textureFor });
        ui.endFrame();
        return action;
    };
    const auto row = [&](TileType tile) -> std::optional<Vec2> {
        const auto command = std::ranges::find_if(ui.drawData().commands, [&](const UiDrawCommand& candidate) {
            const Vec2 center = candidate.rect.position + candidate.rect.size * 0.5f;
            return candidate.kind == UiDrawKind::TextureImage && candidate.texture == textureFor(tile) &&
                candidate.rect.size == Vec2 { 28.0f, 28.0f } && candidate.clip && ui.contains(*candidate.clip, center);
        });
        return command != ui.drawData().commands.end()
            ? std::optional { command->rect.position + command->rect.size * 0.5f } : std::nullopt;
    };
    const auto click = [](Vec2 position) {
        return UiPointerInput { .position = position, .pressedAt = position, .releasedAt = position };
    };
    const auto noAction = [](const MakerViewAction& action) {
        return !action.tile && action.command == MakerCommand::None;
    };
    const auto search = [&](std::string_view query) {
        CHECK(view.focusTileSearch());
        auto* edit = view.activeTextEdit();
        CHECK(edit != nullptr);
        if (edit) edit->setText(query);
        (void)draw();
    };

    (void)draw();
    CHECK(row(TileType::Wall) && row(TileType::CliffWall));
    constexpr float centerYInFirstHeader = 88.0f + 14.0f;
    // Click near the right edge to exercise the full-width header hit area.
    const Vec2 basics = layout.palette.position + Vec2 { layout.palette.size.x - 23.0f, centerYInFirstHeader };
    const Vec2 terrainBelowClosedBasics = basics + Vec2 { 0.0f, 28.0f + 9.0f };
    const Vec2 board = layout.board.position + layout.board.size * 0.5f;
    CHECK(noAction(draw({ .position = basics, .down = true, .pressedAt = basics })));
    CHECK(noAction(draw({ .position = board, .releasedAt = board })));
    CHECK(row(TileType::Wall).has_value());

    CHECK(noAction(draw(click(basics))));
    (void)draw();
    CHECK(!row(TileType::Ground) && !row(TileType::Wall) && row(TileType::CliffWall));
    // The next keyboard stop after a collapsed header is the next section.
    CHECK(noAction(draw({}, { .next = true })));
    CHECK(noAction(draw({}, { .activate = true })));
    (void)draw();
    CHECK(!row(TileType::Wall) && !row(TileType::CliffWall) && row(TileType::Rogue));
    CHECK(noAction(draw({}, { .activate = true })));
    (void)draw();
    CHECK(!row(TileType::Wall) && row(TileType::CliffWall));

    CHECK(noAction(draw(click(terrainBelowClosedBasics))));
    (void)draw();
    CHECK(!row(TileType::Wall) && !row(TileType::CliffWall));

    search("wall");
    CHECK(row(TileType::Wall) && row(TileType::CliffWall));
    CHECK(noAction(draw(click(basics))));
    (void)draw();
    CHECK(!row(TileType::Wall) && row(TileType::CliffWall));
    search("barrier wall");
    CHECK(row(TileType::Wall) && !row(TileType::CliffWall));
    search("   ");
    CHECK(!row(TileType::Wall) && !row(TileType::CliffWall) && row(TileType::Rogue));
    search("wall");
    CHECK(row(TileType::Wall) && row(TileType::CliffWall));
    search("");
    CHECK(!row(TileType::Wall) && !row(TileType::CliffWall));

    CHECK(draw(click({ viewport.x - 206.0f, 31.0f })).command == MakerCommand::TestPlay);
    ui.beginFrame(viewport, click({ viewport.x - 100.0f, 31.0f }));
    CHECK(view.drawPlaytest(ui, MakerView::playtestLayout(viewport), false) == MakerCommand::ReturnToMaker);
    ui.endFrame();
    (void)draw();
    CHECK(!row(TileType::Wall) && !row(TileType::CliffWall));
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
    // A complete palette traversal also steps through headings and toolbar controls.
    for (std::size_t frame = 0; frame < 2 * tileTypeCount; ++frame) {
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
    constexpr Vec2 toolbarSave { 260.0f, 31.0f };
    constexpr Vec2 toolbarLoad { 334.0f, 31.0f };
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
    CHECK(view.draftNameFocused());
    const MakerDraftDialogState list { .mode = MakerDraftDialogMode::Load, .names = drafts };
    CHECK(draw(list).command == Command::None);
    constexpr Vec2 secondRow { 110.0f, 191.0f };
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

    view.beginDraftDialog(MakerDraftDialogMode::Load);
    CHECK(view.activeDraftNameEdit() != nullptr);
    if (auto* search = view.activeDraftNameEdit()) {
        search->setText("17 draFT");
        const auto filtered = draw(list, {}, { .navigation = { .activate = true } });
        CHECK(filtered.command == Command::Submit && filtered.name == drafts[16]);
        search->setText("no matching draft");
        CHECK(draw(list, {}, { .navigation = { .activate = true } }).command == Command::None);
        CHECK(draw(list, { .position = loadButton, .pressedAt = loadButton, .releasedAt = loadButton }).command ==
            Command::None);
    }
    constexpr Vec2 searchField { 180.0f, 106.0f };
    CHECK(draw(list, { .position = searchField, .pressedAt = searchField, .releasedAt = searchField }).command ==
        Command::None);
    CHECK(view.draftNameFocused());

    for (const auto mode : { MakerDraftDialogMode::Overwrite, MakerDraftDialogMode::Discard }) {
        view.beginDraftDialog(mode);
        const MakerDraftDialogState confirmation { .mode = mode, .pendingName = drafts[13] };
        CHECK(draw(confirmation, {}, { .navigation = { .activate = true } }).command == Command::Cancel);
        CHECK(draw(confirmation, {}, { .navigation = { .next = true } }).command == Command::None);
        const auto confirm = draw(confirmation, {}, { .navigation = { .activate = true } });
        CHECK(confirm.command == Command::Submit && confirm.name == drafts[13]);
    }
}

void testDraftRowActionsAndRecoveryDialogs(const FontAtlas& font)
{
    TEST("makerDraftRowActionsAndRecoveryDialogs");
    using Command = MakerDraftDialogAction::Command;
    using Mode = MakerDraftDialogMode;
    constexpr Vec2 viewport { 640.0f, 480.0f };
    UiContext ui(font);
    MakerView view;
    const auto draw = [&](const MakerDraftDialogState& state, UiPointerInput pointer = {},
                          UiNavigation navigation = {}, int rowActionStep = 0) {
        ui.beginFrame(viewport, pointer);
        const auto action = view.drawDraftDialog(ui, viewport, 1.0f, state,
            { .navigation = navigation, .pickerRowActionStep = rowActionStep });
        ui.endFrame();
        return action;
    };
    const auto click = [](Vec2 position) {
        return UiPointerInput { .position = position, .pressedAt = position, .releasedAt = position };
    };
    const std::array<std::string, 2> names { "First", "Second" };
    // The controls at the right of the second row act on it while the first is selected.
    constexpr Vec2 renameSecond { 434.0f, 196.0f };
    constexpr Vec2 deleteSecond { 504.0f, 196.0f };
    for (bool overworld : { false, true }) {
        const MakerDraftDialogState load { .mode = Mode::Load, .names = names, .overworld = overworld };
        view.beginDraftDialog(Mode::Load, names[0]);
        CHECK(draw(load).command == Command::None);
        const auto rename = draw(load, click(renameSecond));
        CHECK(rename.command == Command::Rename && rename.name == names[1]);
        view.beginDraftDialog(Mode::RenameDraft, rename.name);
        CHECK(view.draftNameFocused());
        CHECK(view.submitDraftName().name == names[1]);

        view.beginDraftDialog(Mode::Load, names[0]);
        CHECK(draw(load).command == Command::None);
        const auto remove = draw(load, click(deleteSecond));
        CHECK(remove.command == Command::Delete && remove.name == names[1]);
        view.beginDraftDialog(Mode::DeleteDraft);
        const MakerDraftDialogState confirm { .mode = Mode::DeleteDraft, .pendingName = remove.name, .overworld = overworld };
        CHECK(draw(confirm, {}, { .activate = true }).command == Command::Cancel);
        CHECK(draw(confirm, {}, { .next = true }).command == Command::None);
        const auto accepted = draw(confirm, {}, { .activate = true });
        CHECK(accepted.command == Command::Submit && accepted.name == names[1]);
    }

    const MakerDraftDialogState load { .mode = Mode::Load, .names = names };
    view.beginDraftDialog(Mode::Load, names[1]);
    CHECK(draw(load).command == Command::None);
    CHECK(draw(load, {}, {}, 1).command == Command::None);
    CHECK(draw(load, {}, {}, 1).command == Command::None);
    const auto keyboardDelete = draw(load, {}, { .activate = true });
    CHECK(keyboardDelete.command == Command::Delete && keyboardDelete.name == names[1]);

    view.beginDraftDialog(Mode::Load);
    auto* search = view.activeDraftNameEdit();
    CHECK(search != nullptr);
    if (search) {
        search->setText(names[1]);
        CHECK(draw(load).command == Command::None);
        const auto filteredDelete = draw(load, click({ deleteSecond.x, deleteSecond.y - 41.0f }));
        CHECK(filteredDelete.command == Command::Delete && filteredDelete.name == names[1]);
    }

    view.beginDraftDialog(Mode::RecoverAutosave);
    const MakerDraftDialogState recovery { .mode = Mode::RecoverAutosave, .pendingName = "Workshop" };
    CHECK(draw(recovery, {}, { .activate = true }).command == Command::Submit);
    const auto recoverDialog = UiDialogLayout::centered(viewport, 1.0f, { 510.0f, 286.0f }, 316.0f);
    const Vec2 discard { recoverDialog.content.position.x + 20.0f, recoverDialog.footerButton(132.0f).position.y + 10.0f };
    CHECK(draw(recovery, { .position = discard, .pressedAt = discard, .releasedAt = discard }).command == Command::Discard);
    view.beginDraftDialog(Mode::DiscardAutosave);
    const MakerDraftDialogState confirm { .mode = Mode::DiscardAutosave, .pendingName = "Workshop" };
    CHECK(draw(confirm, {}, { .activate = true }).command == Command::Cancel);
    view.beginDraftDialog(Mode::RecoverAutosave);
    CHECK(draw({ .mode = Mode::RecoverAutosave, .canRecoverAutosave = false }, {}, { .activate = true }).command != Command::Submit);
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
    constexpr Vec2 toolbarNewDraft { 174.0f, 31.0f };
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
    ui.beginFrame(wideViewport, click({ 841.0f, 31.0f }));
    CHECK(view.draw(ui, MakerView::layout(wideViewport), {}).command == MakerCommand::ResizeBoard);
    ui.endFrame();

    const auto draw = [&](MakerDraftDialogMode mode, UiPointerInput pointer = {}, UiNavigation navigation = {}) {
        ui.beginFrame(viewport, pointer);
        const auto editor = view.draw(ui, layout, { .showViewportControls = false }, { .interactive = false });
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
    constexpr Vec2 connectButton { 130.0f, 69.0f };
    ui.beginFrame(viewport, click(connectButton));
    CHECK(view.draw(ui, layout, {}).command == MakerCommand::Connect);
    ui.endFrame();
    CHECK(std::ranges::count_if(ui.drawData().commands, [&](const UiDrawCommand& command) {
        return command.kind == UiDrawKind::FontGlyph && command.clip &&
            ui.contains(*command.clip, connectButton);
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

    const std::array colors { Vec3 { 0.14f, 0.46f, 0.22f }, Vec3 { 0.9f, 0.21f, 0.31f } };
    auto colored = state;
    colored.linkInspector->groupColor = colors.front();
    colored.linkInspector->groupColors = colors;
    ui.beginFrame(viewport, click({ 56.0f, 247.0f }));
    CHECK(view.draw(ui, layout, colored).command == MakerCommand::EditLinkGroupColor);
    ui.endFrame();
    const auto linear = ui::color::displayLinear(colors.front());
    CHECK(std::ranges::any_of(ui.drawData().commands, [&](const UiDrawCommand& command) {
        return command.kind == UiDrawKind::Solid &&
            command.color == Vec4 { linear.x, linear.y, linear.z, 1.0f } &&
            command.clip && ui.contains(*command.clip, { 56.0f, 247.0f });
    }));

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
    constexpr Vec2 layerButton { 179.0f, 69.0f };
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

void testOverworldMakerControls(const FontAtlas& font)
{
    TEST("makerOverworldControlsAndPalette");
    constexpr Vec2 viewport { 640.0f, 480.0f };
    const auto layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    MakerViewState state;
    const auto draw = [&](UiPointerInput pointer = {}) {
        ui.beginFrame(viewport, pointer);
        const auto action = view.draw(ui, layout, state);
        ui.endFrame();
        return action;
    };
    const auto click = [&](Vec2 position) {
        return draw({ .position = position, .pressedAt = position, .releasedAt = position });
    };
    const Vec2 modeButton = layout.palette.position + Vec2 { layout.palette.size.x - 55.0f, 23.0f };
    const Vec2 neighborsButton { viewport.x - 75.0f, 70.0f };
    const Vec2 firstTile = layout.palette.position + Vec2 { 100.0f, 132.0f };
    const Vec2 previous = layout.footer.position + Vec2 { 25.0f, 17.0f };
    const Vec2 next = layout.footer.position + Vec2 { 55.0f, 17.0f };

    CHECK(click(modeButton).command == MakerCommand::ToggleOverworldMode);
    CHECK(click(neighborsButton).command == MakerCommand::ResizeBoard);
    CHECK(state.showNeighbors);
    state.overworld = true;
    CHECK(click(neighborsButton).command == MakerCommand::ToggleOverworldNeighbors);
    state.showNeighbors = false;
    CHECK(click(neighborsButton).command == MakerCommand::ToggleOverworldNeighbors);
    CHECK(click(previous).command == MakerCommand::None);
    state.hasMultipleOverworldScreens = true;
    CHECK(click(previous).command == MakerCommand::PreviousOverworldScreen);
    CHECK(click(next).command == MakerCommand::NextOverworldScreen);
    CHECK(click(modeButton).command == MakerCommand::ToggleOverworldMode);

    const auto search = [&](std::string_view query) {
        if (!view.focusTileSearch()) return MakerViewAction {};
        view.activeTextEdit()->setText(query);
        (void)draw();
        view.blurTileSearch();
        return click(firstTile);
    };
    CHECK(search("lorekeeper player").tile == TileType::Player);
    CHECK(search("wardrobe paladin").tile == TileType::WardrobeRogue);
    CHECK(makerTileGuide::name(TileType::WardrobeCrystalDruid, true) == "Wardrobe Druid");
    CHECK(search("wardrobe druid").tile == TileType::WardrobeCrystalDruid);
    CHECK(!search("wardrobe lorekeeper").tile);
    CHECK(search("lectern message").tile == TileType::LecternNorth);
    const auto selector = search("level selector");
    CHECK(selector.command == MakerCommand::PaintSelector && !selector.tile);
    CHECK(!search("objective").tile);
    CHECK(!search("swaps witch").tile);
    state.overworld = false;
    CHECK(search("level selector").command == MakerCommand::None);
    CHECK(!search("lectern message").tile);
    CHECK(search("objective").tile == TileType::End);
}

void testMakerAreaToolControls(const FontAtlas& font)
{
    TEST("makerAreaToolControls");
    constexpr Vec2 viewport { 640.0f, 480.0f };
    const auto layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    MakerViewState state;
    const auto draw = [&](UiPointerInput pointer = {}) {
        ui.beginFrame(viewport, pointer);
        const auto action = view.draw(ui, layout, state);
        ui.endFrame();
        return action.command;
    };
    const auto click = [&](float x, float y) {
        const Vec2 position = layout.palette.position + Vec2 { x, y };
        return draw({ .position = position, .pressedAt = position, .releasedAt = position });
    };
    CHECK(click(78.0f, 23.0f) == MakerCommand::SelectArea);
    state.area.mode = MakerAreaViewState::Mode::Rectangle;
    CHECK(click(54.0f, 102.0f) == MakerCommand::FloodFill);
    CHECK(click(145.0f, 102.0f) == MakerCommand::None);
    state.area.hasClipboard = true;
    CHECK(click(145.0f, 102.0f) == MakerCommand::PasteArea);
    state.area.hasSelection = true;
    CHECK(click(33.0f, 137.0f) == MakerCommand::FillSelection);
    CHECK(click(80.0f, 137.0f) == MakerCommand::CopySelection);
    CHECK(click(125.0f, 137.0f) == MakerCommand::CutSelection);
    CHECK(click(168.0f, 137.0f) == MakerCommand::EraseSelection);

    constexpr Vec4 outlineColor { 1.0f, 0.78f, 0.35f, 1.0f };
    const Vec2 base = layout.board.position + Vec2 { 30.0f, 30.0f };
    state.area.outline = MakerAreaViewState::Outline {
        { base, base + Vec2 { 80.0f, 0.0f }, base + Vec2 { 95.0f, 40.0f }, base + Vec2 { 12.0f, 40.0f } },
        outlineColor,
    };
    (void)draw();
    CHECK(std::ranges::any_of(ui.drawData().commands, [&](const UiDrawCommand& command) {
        return command.kind == UiDrawKind::Solid && command.color == outlineColor && command.clip == layout.board;
    }));
}

void testMakerLayersAndProperties(const FontAtlas& font)
{
    TEST("makerLayersAndProperties");
    constexpr Vec2 viewport { 640.0f, 480.0f };
    const MakerLayout layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    MakerViewState state { .layer = 1, .layerCount = 3 };
    const auto click = [&](Vec2 at, bool interactive = true) {
        ui.beginFrame(viewport, { .position = at, .pressedAt = at, .releasedAt = at });
        const auto action = view.draw(ui, layout, state, { .interactive = interactive });
        ui.endFrame();
        return action;
    };
    CHECK(click({ 72.0f, 414.0f }).command == MakerCommand::ShowLayers);
    CHECK(click({ 122.0f, 414.0f }).command == MakerCommand::ShowProperties);
    state.panel = MakerPanel::Layers;
    state.layerPanel = { .waterLayer = 1, .canAdd = true, .canDelete = true, .canSetWater = true };
    CHECK(click({ 66.0f, 162.0f }).command == MakerCommand::AddLayerAbove);
    CHECK(click({ 157.0f, 162.0f }).command == MakerCommand::AddLayerBelow);
    CHECK(click({ 104.0f, 196.0f }).command == MakerCommand::ToggleWaterLayer);
    CHECK(click({ 104.0f, 232.0f }).command == MakerCommand::ToggleLayerLock);
    CHECK(click({ 104.0f, 266.0f }).command == MakerCommand::DeleteLayer);
    const auto selectLayer = click({ 104.0f, 303.0f });
    CHECK(selectLayer.command == MakerCommand::SelectLayer && selectLayer.layer == 2);
    state.layerPanel.canDelete = false;
    CHECK(click({ 104.0f, 266.0f }).command == MakerCommand::None);
    CHECK(click({ 33.0f, 414.0f }).command == MakerCommand::ShowPalette);
    CHECK(!view.focusTileSearch());

    state.panel = MakerPanel::Properties;
    state.propertyPanel = { .cell = GridPosition3 { 2, 4, 1 }, .tile = TileType::Gate, .gateStartOpen = false };
    CHECK(click({ 104.0f, 232.0f }).command == MakerCommand::ToggleGateStartOpen);
    CHECK(click({ 104.0f, 232.0f }, false).command == MakerCommand::None);
    state.propertyPanel = { .cell = GridPosition3 { 2, 4, 1 }, .tile = TileType::LockPlate, .lockPlateStartEnabled = true };
    CHECK(click({ 104.0f, 232.0f }).command == MakerCommand::ToggleLockPlateStartEnabled);
    constexpr std::array<uint8_t, 2> directions { 0, 2 };
    state.propertyPanel = { .cell = GridPosition3 { 2, 4, 1 }, .tile = TileType::Minecart,
        .minecartDirection = 0, .minecartDirections = directions, .minecartRoute = "Two stops" };
    CHECK(click({ 157.0f, 232.0f }).command == MakerCommand::None);
    const auto direction = click({ 66.0f, 267.0f });
    CHECK(direction.command == MakerCommand::SetMinecartDirection && direction.minecartDirection == 2);
}

void testMakerProblems(const FontAtlas& font)
{
    TEST("makerProblems");
    constexpr Vec2 viewport { 640.0f, 480.0f };
    const MakerLayout layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    MakerViewState state;
    const auto draw = [&](UiPointerInput pointer = {}, MakerViewInput input = {}) {
        ui.beginFrame(viewport, pointer);
        const auto action = view.draw(ui, layout, state, input);
        ui.endFrame();
        return action;
    };
    const auto click = [&](Vec2 at, bool interactive = true) {
        return draw({ .position = at, .pressedAt = at, .releasedAt = at }, { .interactive = interactive });
    };
    CHECK(click({ 182.0f, 414.0f }).command == MakerCommand::ShowProblems);
    state.panel = MakerPanel::Problems;
    const Vec2 firstRow = layout.palette.position + Vec2 { 80.0f, 60.0f };
    CHECK(click(firstRow).command == MakerCommand::None);
    CHECK(!view.focusTileSearch());

    using Kind = MakerSession::ProblemKind;
    std::array<MakerSession::Problem, 12> problems;
    std::ranges::fill(problems, MakerSession::Problem {
        Kind::UnpairedPortal, 2, GridPosition3 { 4, 3, 1 }, "Portal has no partner.",
    });
    problems.front() = { Kind::MissingPlayerStart, 1, {}, "Place a Lorekeeper to set the player start." };
    problems.back() = { Kind::BrokenLevelSelector, 6, GridPosition3 { 9, 7, 0 }, "Level Selector has no assigned puzzle." };
    state.problems = problems;
    state.linkInspector = MakerLinkInspector {};
    const Vec2 marker = layout.board.position + Vec2 { 150.0f, 140.0f };
    state.problemMarker = marker;
    const auto first = click(firstRow);
    CHECK(first.command == MakerCommand::JumpToProblem && first.problem == 0);
    CHECK(std::ranges::any_of(ui.drawData().commands, [&](const UiDrawCommand& command) {
        return command.kind == UiDrawKind::Solid && command.clip == layout.board &&
            command.rect.position + command.rect.size * 0.5f == marker;
    }));
    CHECK(click(firstRow, false).command == MakerCommand::None);
    for (std::size_t index = 1; index < 9; ++index) {
        const auto keyboard = draw({}, { .navigation = { .next = true, .activate = true } });
        CHECK(keyboard.command == MakerCommand::JumpToProblem && keyboard.problem == index);
    }
    (void)draw({ .position = firstRow }, { .wheelY = -20.0f, .wheelPosition = firstRow });
    const auto last = click({ firstRow.x, 370.0f });
    CHECK(last.command == MakerCommand::JumpToProblem && last.problem == problems.size() - 1);
    state.problems = {};
    CHECK(click(firstRow).command == MakerCommand::None);
}

void testMakerViewportAndOverworldMapControls(const FontAtlas& font)
{
    TEST("makerViewportAndOverworldMapControls");
    constexpr Vec2 viewport { 800.0f, 700.0f };
    const MakerLayout layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    const auto editorClick = [&](Vec2 at, bool overworld = false) {
        ui.beginFrame(viewport, { .position = at, .pressedAt = at, .releasedAt = at });
        const auto action = view.draw(ui, layout, { .overworld = overworld });
        ui.endFrame();
        return action.command;
    };
    CHECK(editorClick(layout.board.position + Vec2 { 50.0f, 24.0f }) == MakerCommand::ToggleTopDown);
    CHECK(editorClick(layout.board.position + Vec2 { 170.0f, 24.0f }) == MakerCommand::FrameSelection);
    CHECK(editorClick(layout.board.position + Vec2 { 308.0f, 24.0f }) == MakerCommand::ToggleBoardCursor);
    CHECK(editorClick(layout.footer.position + Vec2 { 100.0f, 17.0f }) == MakerCommand::None);
    CHECK(editorClick(layout.footer.position + Vec2 { 100.0f, 17.0f }, true) == MakerCommand::OverworldMap);

    using Command = MakerOverworldMapAction::Command;
    std::array screens {
        OverworldScreenSpec { .id = 1, .slot = { 0, 0 } },
        OverworldScreenSpec { .id = 2, .slot = { 1, 0 } },
    };
    MakerOverworldMapState state { .screens = screens, .activeScreen = 1, .canUndo = true, .canRedo = true };
    const auto click = [&](Vec2 at) {
        ui.beginFrame(viewport, { .position = at, .pressedAt = at, .releasedAt = at });
        const auto action = view.drawOverworldMap(ui, viewport, 1.0f, state);
        ui.endFrame();
        return action;
    };
    constexpr Vec2 first { 366.0f, 355.0f };
    constexpr Vec2 second { 434.0f, 355.0f };
    constexpr Vec2 empty { 502.0f, 355.0f };
    constexpr Vec2 move { 96.0f, 626.0f };
    constexpr Vec2 remove { 238.0f, 626.0f };
    constexpr Vec2 confirm { 538.0f, 408.0f };
    view.beginOverworldMap();
    CHECK(click(empty).command == Command::None);
    const auto jump = click(second);
    CHECK(jump.command == Command::Jump && jump.screen == 2);
    state.activeScreen = jump.screen;
    CHECK(click(move).command == Command::None);
    CHECK(click(first).command == Command::None);
    const auto changed = click(empty);
    CHECK((changed.command == Command::Move && changed.screen == 2 && changed.slot == OverworldSlot { 2, 0 }));
    screens[1].slot = changed.slot;
    CHECK(click({ 380.0f, 626.0f }).command == Command::Undo);
    CHECK(click({ 522.0f, 626.0f }).command == Command::Redo);
    CHECK(click(move).command == Command::None);
    CHECK(view.cancelOverworldMapAction());
    CHECK(!view.cancelOverworldMapAction());
    CHECK(click(remove).command == Command::None);
    CHECK(click(first).command == Command::None);
    const auto deleted = click(confirm);
    CHECK(deleted.command == Command::Delete && deleted.screen == 2);
    state.screens = std::span(screens).first(1);
    state.activeScreen = 1;
    CHECK(click(remove).command == Command::None);
    CHECK(!view.cancelOverworldMapAction());
    CHECK(click({ 710.0f, 68.0f }).command == Command::Close);
}

void testMakerToolbarOverflow(const FontAtlas& font)
{
    TEST("makerToolbarOverflowOwnsInputAndRevealsCommands");
    constexpr Vec2 viewport { 480.0f, 360.0f };
    const auto layout = MakerView::layout(viewport);
    UiContext ui(font);
    MakerView view;
    const auto click = [&](Vec2 at, bool interactive = true) {
        ui.beginFrame(viewport, { .position = at, .pressedAt = at, .releasedAt = at });
        const auto action = view.draw(ui, layout, { .layerCount = 2 }, { .interactive = interactive });
        const bool boardClick = ui.clicked(layout.board);
        ui.endFrame();
        return std::pair { action, boardClick };
    };
    constexpr Vec2 more { 322.0f, 31.0f };
    CHECK(click({ 235.0f, 31.0f }).first.command == MakerCommand::TestPlay);
    CHECK(click(more, false).first.command == MakerCommand::None && !view.toolbarOverflowOpen());
    CHECK(click(more).first.command == MakerCommand::None && view.toolbarOverflowOpen());
    CHECK(click({ 230.0f, 77.0f }).first.command == MakerCommand::None && view.toolbarOverflowOpen());
    CHECK(click({ 230.0f, 145.0f }).first.command == MakerCommand::Camera && !view.toolbarOverflowOpen());
    CHECK(click(more).first.command == MakerCommand::None && view.toolbarOverflowOpen());
    const auto outside = click({ 465.0f, 210.0f });
    CHECK(outside.first.command == MakerCommand::None && !outside.first.tile && !outside.second);
    CHECK(!view.toolbarOverflowOpen());
    CHECK(click(more).first.command == MakerCommand::None && view.closeToolbarOverflow());
    CHECK(!view.closeToolbarOverflow());

    constexpr Vec2 narrow { 360.0f, 240.0f };
    const auto narrowLayout = MakerView::layout(narrow);
    const auto navigate = [&](UiPointerInput pointer, UiNavigation navigation = {}) {
        ui.beginFrame(narrow, pointer);
        const auto action = view.draw(ui, narrowLayout, {}, { .navigation = navigation });
        ui.endFrame();
        return action.command;
    };
    constexpr Vec2 narrowMore { 245.0f, 31.0f };
    CHECK(navigate({ .position = narrowMore, .pressedAt = narrowMore, .releasedAt = narrowMore }) == MakerCommand::None);
    CHECK(view.toolbarOverflowOpen());
    CHECK(navigate({}) == MakerCommand::None);
    for (int i = 0; i < 7; ++i) CHECK(navigate({}, { .next = true }) == MakerCommand::None);
    CHECK(navigate({}, { .activate = true }) == MakerCommand::ToggleBoardCursor);
    CHECK(!view.toolbarOverflowOpen());
}

void testOverworldItemDialogs(const FontAtlas& font)
{
    TEST("makerOverworldItemDialogs");
    using Command = MakerDraftDialogAction::Command;
    constexpr auto selector = MakerDraftDialogMode::SelectorTarget;
    constexpr auto lectern = MakerDraftDialogMode::LecternText;
    constexpr Vec2 viewport { 800.0f, 600.0f };
    const std::array names { std::string("Clockwork Hall"), std::string("Moonlit Rails"),
        std::string("Rail Bridge") };
    UiContext ui(font);
    MakerView view;
    const auto accept = [&](MakerDraftDialogMode mode, std::string_view current = {}) {
        ui.beginFrame(viewport, UiPointerInput {});
        const auto result = view.drawDraftDialog(ui, viewport, 1.0f,
            { .mode = mode, .names = names, .pendingName = current },
            { .navigation = { .activate = true } });
        ui.endFrame();
        return result;
    };
    view.beginDraftDialog(selector, names[2]);
    const auto target = accept(selector);
    CHECK(target.command == Command::Submit && target.name == names[2]);
    view.beginDraftDialog(selector);
    const auto currentTarget = accept(selector, names[1]);
    CHECK(currentTarget.command == Command::Submit && currentTarget.name == names[1]);
    view.beginDraftDialog(selector);
    ui.beginFrame(viewport, UiPointerInput {});
    const auto missing = view.drawDraftDialog(ui, viewport, 1.0f,
        { .mode = selector, .names = names, .pendingName = names[0], .missingSelectorTarget = true },
        { .navigation = { .activate = true }, .pickerRowActionStep = 1 });
    ui.endFrame();
    CHECK(missing.command == Command::Submit && missing.name.empty());
    view.beginDraftDialog(selector, names[0]);
    ui.beginFrame(viewport, UiPointerInput {});
    const auto detach = view.drawDraftDialog(ui, viewport, 1.0f,
        { .mode = selector, .names = names, .pendingName = names[0] },
        { .navigation = { .previous = true, .activate = true } });
    ui.endFrame();
    CHECK(detach.command == Command::Submit && detach.name.empty());
    view.beginDraftDialog(selector);
    auto* search = view.activeDraftNameEdit();
    CHECK(search != nullptr);
    if (search) {
        search->setText("rAiLs moon");
        const auto filtered = accept(selector);
        CHECK(filtered.command == Command::Submit && filtered.name == names[1]);
        search->setText("no matching level");
        CHECK(accept(selector).command == Command::None);
    }

    view.beginDraftDialog(lectern, R"(First line\nSecond line)");
    CHECK(view.draftNameFocused());
    const auto initial = accept(lectern);
    CHECK(initial.command == Command::Submit && initial.name == R"(First line\nSecond line)");
    auto* edit = view.activeDraftNameEdit();
    CHECK(edit != nullptr);
    if (!edit) return;
    edit->setText("");
    const auto cleared = view.submitDraftName();
    CHECK(cleared.command == Command::Submit && cleared.name.empty());
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
    testHelpDialog(font);
    testCameraDialog(font);
    testPaletteSearchFiltersWithinGroups(font);
    testPaletteGroupCollapse(font);
    testPaletteSelectsDistinctObjectsAndRotators(font);
    testPaletteThumbnailsAndBoardGuide(font);
    testPaletteKeyboardRevealsRows(font);
    testPaletteSearchPressOwnsGesture(font);
    testDraftSaveDialog(font);
    testDraftLoadDialog(font);
    testDraftRowActionsAndRecoveryDialogs(font);
    testNewDraftDialog(font);
    testResizeBoardDialog(font);
    testLinkInspectorNativeControls(font);
    testLinkLayerAndElevatorControls(font);
    testOverworldMakerControls(font);
    testMakerAreaToolControls(font);
    testMakerLayersAndProperties(font);
    testMakerProblems(font);
    testMakerViewportAndOverworldMapControls(font);
    testMakerToolbarOverflow(font);
    testOverworldItemDialogs(font);
    std::cout << "MakerViewTests: " << checks - failures << " of " << checks
              << " checks passed\n";
    return failures != 0 ? 1 : 0;
}
