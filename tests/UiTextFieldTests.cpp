#include "TestHarness.hpp"

#include "engine/ui/FontAtlas.hpp"
#include "engine/ui/UiTextEdit.hpp"
#include "engine/ui/UiTextField.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace {

using namespace sokoban;

constexpr Vec2 viewport { 640.0f, 360.0f };
constexpr UiRect fieldBounds { { 20.0f, 20.0f }, { 220.0f, 32.0f } };
constexpr UiTextFieldOptions focused { .focused = true };

void testCaretSpans(const FontAtlas& font)
{
    TEST("editableLayoutUsesShapedGraphemePositions");
    constexpr std::string_view accent = "e\xcc\x81";
    const TextLayout& regular = font.layoutText(accent, 18.0f);
    const TextLayout& editable = font.layoutText(accent, 18.0f, TextLayoutPurpose::Editing);
    CHECK(regular.caretSpans.empty());
    CHECK(editable.glyphs.size() == regular.glyphs.size());
    CHECK(editable.extent == regular.extent);
    CHECK(editable.caretSpans.size() == 1);
    if (!editable.caretSpans.empty()) {
        const auto& span = editable.caretSpans.front();
        CHECK(span.start == 0 && span.end == accent.size());
        CHECK(span.startX == 0.0f && span.endX == editable.extent.x);
    }

    const auto& rtl = font.layoutText("\xd7\x90\xd7\x91\xd7\x92", 18.0f, TextLayoutPurpose::Editing);
    CHECK(rtl.caretSpans.size() == 3);
    if (rtl.caretSpans.size() == 3) {
        CHECK(rtl.caretSpans.front().start == 4 && rtl.caretSpans.front().endX == 0.0f);
        CHECK(rtl.caretSpans.back().start == 0 && rtl.caretSpans.back().startX == rtl.extent.x);
        CHECK(std::ranges::all_of(rtl.caretSpans, [](TextCaretSpan span) { return span.startX > span.endX; }));
    }

    // Arabic lam-alef remains two editable graphemes even if shaped as one glyph.
    const auto& joined = font.layoutText("\xd9\x84\xd8\xa7", 18.0f, TextLayoutPurpose::Editing);
    CHECK(joined.caretSpans.size() == 2);
    if (joined.caretSpans.size() == 2) {
        CHECK(joined.caretSpans.front().start == 2);
        CHECK(joined.caretSpans.front().startX == joined.caretSpans.back().endX);
        CHECK(joined.caretSpans.back().startX == joined.extent.x);
    }
}

void testPointerSelection(const FontAtlas& font)
{
    TEST("textFieldSelectsFromActualPressAndRelease");
    UiContext ui(font);
    UiTextField field;
    UiTextEdit edit;
    ui.beginFrame(viewport, UiPointerInput {});
    const float left = field.draw(ui, "test", fieldBounds, edit, focused).caret.position.x;
    ui.endFrame();

    edit.setText("a" "e\xcc\x81" "bc");
    const auto& layout = font.layoutText(edit.text(), 18.0f, TextLayoutPurpose::Editing);
    if (layout.caretSpans.size() != 4) {
        CHECK(layout.caretSpans.size() == 4);
        return;
    }
    const Vec2 press { left + layout.caretSpans.front().endX, 30.0f };
    const Vec2 release { left + layout.caretSpans[1].endX, 130.0f };
    ui.beginFrame(viewport, { .position = { 500.0f, 300.0f }, .pressedAt = press, .releasedAt = release });
    const auto result = field.draw(ui, "test", fieldBounds, edit);
    ui.endFrame();
    CHECK(result.pressed);
    CHECK(edit.anchor() == 1);
    CHECK(edit.caret() == 4);
    CHECK(edit.selectedText() == "e\xcc\x81");

    ui.beginFrame(viewport, { .position = press, .down = true, .pressedAt = press });
    const auto disabled = field.draw(ui, "test", fieldBounds, edit, { .focused = true, .enabled = false });
    ui.endFrame();
    CHECK(!disabled.pressed);
    CHECK(disabled.caret.size.x == 0.0f);
    CHECK(edit.selectedText() == "e\xcc\x81");
}

void testEventTimeCaretPlacement(const FontAtlas& font)
{
    TEST("eventTimeCaretPlacementSurvivesFollowingTextInput");
    UiContext ui(font);
    UiTextField field;
    UiTextEdit edit;
    ui.beginFrame(viewport, UiPointerInput {});
    const float left = field.draw(ui, "test", fieldBounds, edit, focused).caret.position.x;
    ui.endFrame();

    edit.setText("abc");
    const auto& layout = font.layoutText(edit.text(), 18.0f, TextLayoutPurpose::Editing);
    CHECK(layout.caretSpans.size() == 3);
    if (layout.caretSpans.size() != 3) return;
    const Vec2 press { left + layout.caretSpans.front().endX, 30.0f };
    field.placeCaret(ui, fieldBounds, edit, 18.0f, press, false);
    CHECK(edit.caret() == 1);
    CHECK(edit.insert("X"));

    ui.beginFrame(viewport, { .position = press, .down = true, .pressedAt = press });
    const auto captured = field.draw(ui, "test", fieldBounds, edit,
        { .focused = true, .pressAlreadyHandled = true });
    ui.endFrame();
    CHECK(captured.pressed);
    CHECK(edit.text() == "aXbc" && edit.caret() == 2 && edit.anchor() == 2);

    ui.beginFrame(viewport, { .position = press, .releasedAt = press });
    (void)field.draw(ui, "test", fieldBounds, edit, focused);
    ui.endFrame();
    CHECK(edit.caret() == 2 && edit.anchor() == 2);

    ui.beginFrame(viewport, { .position = press, .pressedAt = press, .releasedAt = press });
    const auto handledOutside = field.draw(ui, "test", fieldBounds, edit, { .pressAlreadyHandled = true });
    ui.endFrame();
    CHECK(!handledOutside.pressed && handledOutside.caret.size.x == 0.0f);
    CHECK(edit.caret() == 2 && edit.anchor() == 2);
}

void testMixedDirectionCaretPlacement(const FontAtlas& font)
{
    TEST("mixedDirectionCaretUsesTheGraphemeUnderThePointer");
    UiContext ui(font);
    UiTextField field;
    UiTextEdit edit;
    ui.beginFrame(viewport, UiPointerInput {});
    const float left = field.draw(ui, "test", fieldBounds, edit, focused).caret.position.x;
    ui.endFrame();

    edit.setText("A \xd7\x90\xd7\x91");
    const auto& layout = font.layoutText(edit.text(), 18.0f, TextLayoutPurpose::Editing);
    const auto rtl = std::ranges::find_if(layout.caretSpans,
        [](TextCaretSpan span) { return span.startX > span.endX; });
    CHECK(rtl != layout.caretSpans.end());
    if (rtl == layout.caretSpans.end()) return;
    const float x = rtl->endX + (rtl->startX - rtl->endX) * 0.15f;
    field.placeCaret(ui, fieldBounds, edit, 18.0f, { left + x, 30.0f }, false);
    CHECK(edit.caret() == rtl->end);
}

void testHorizontalScrollAndPreedit(const FontAtlas& font)
{
    TEST("textFieldClipsOverflowAndShowsImeComposition");
    UiContext ui(font);
    UiTextField field;
    UiTextEdit edit;
    edit.setText("This name is wider than the field that displays it");
    constexpr UiRect narrow { fieldBounds.position, { 90.0f, 32.0f } };

    ui.beginFrame(viewport, UiPointerInput {});
    const auto end = field.draw(ui, "test", narrow, edit, focused).caret;
    ui.endFrame();
    CHECK(end.size.x > 0.0f);
    CHECK(end.position.x + end.size.x < narrow.position.x + narrow.size.x);
    CHECK(end.position.x > narrow.position.x + narrow.size.x * 0.5f);
    CHECK(std::ranges::all_of(ui.drawData().commands, [&](const UiDrawCommand& command) {
        return command.kind != UiDrawKind::FontGlyph ||
            (command.clip && command.clip->size.x < narrow.size.x);
    }));

    edit.moveCaret(UiTextMovement::Home);
    ui.beginFrame(viewport, UiPointerInput {});
    const auto start = field.draw(ui, "test", narrow, edit, focused).caret;
    ui.endFrame();
    CHECK(start.position.x < narrow.position.x + narrow.size.x * 0.5f);

    edit.setText("abc");
    edit.setCaret(1);
    edit.setCaret(2, true);
    edit.setComposition("\xd7\x90\xd7\x91", 0, 1);
    ui.beginFrame(viewport, UiPointerInput {});
    const auto preedit = field.draw(ui, "test", fieldBounds, edit, focused).caret;
    ui.endFrame();
    CHECK(preedit.size.x > 0.0f);
    CHECK(edit.text() == "abc");
    CHECK(edit.selectedText() == "b");
    CHECK(std::ranges::any_of(ui.drawData().commands, [](const UiDrawCommand& command) {
        return command.kind == UiDrawKind::Solid && command.clip &&
            command.color == Vec4 { 0.56f, 0.86f, 0.80f, 1.0f };
    }));
    CHECK(std::ranges::count_if(ui.drawData().commands, [](const UiDrawCommand& command) {
        return command.kind == UiDrawKind::FontGlyph;
    }) == 4);
}

} // namespace

int main()
{
    const auto font = sokoban::FontAtlas::loadDefault(std::filesystem::path(SOKOBAN_TEST_ASSET_DIR));
    testCaretSpans(font);
    testPointerSelection(font);
    testEventTimeCaretPlacement(font);
    testMixedDirectionCaretPlacement(font);
    testHorizontalScrollAndPreedit(font);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
