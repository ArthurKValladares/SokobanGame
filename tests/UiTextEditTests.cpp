#include "TestHarness.hpp"

#include "engine/ui/MakerTileGuide.hpp"
#include "engine/ui/UiColorValues.hpp"
#include "engine/ui/UiTextEdit.hpp"

#include <array>
#include <limits>
#include <string>
#include <string_view>

namespace {

using namespace sokoban;

constexpr std::string_view accented = "e\xcc\x81";
constexpr std::string_view womanTechnologist =
    "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb";
constexpr std::string_view regionalA = "\xf0\x9f\x87\xa6";
constexpr std::string_view regionalB = "\xf0\x9f\x87\xa7";

void testGraphemeMovementAndDeletion()
{
    TEST("graphemeMovementAndDeletion");
    UiTextEdit edit;
    edit.setText(std::string(accented) + std::string(womanTechnologist) + "X");
    CHECK(edit.caret() == edit.text().size());
    edit.moveCaret(UiTextMovement::Previous);
    CHECK(edit.caret() == accented.size() + womanTechnologist.size());
    edit.moveCaret(UiTextMovement::Previous, true);
    CHECK(edit.caret() == accented.size());
    CHECK(edit.selectedText() == womanTechnologist);
    edit.moveCaret(UiTextMovement::Previous);
    CHECK(edit.selection() == (UiTextRange { accented.size(), accented.size() }));
    edit.moveCaret(UiTextMovement::Previous);
    CHECK(edit.caret() == 0);
    edit.setCaret(2);
    CHECK(edit.caret() == 0);
    CHECK(edit.deleteForward());
    CHECK(edit.text() == std::string(womanTechnologist) + "X");
    edit.moveCaret(UiTextMovement::Next);
    CHECK(edit.caret() == womanTechnologist.size());
    CHECK(edit.backspace());
    CHECK(edit.text() == "X");
    CHECK(edit.caret() == 0);
    CHECK(!edit.backspace());
    edit.moveCaret(UiTextMovement::End);
    CHECK(!edit.deleteForward());
}

void testSelectionReplacesInEitherDirection()
{
    TEST("selectionReplacesInEitherDirection");
    UiTextEdit edit;
    edit.setText("WXYZ");
    edit.moveCaret(UiTextMovement::Home, true);
    CHECK(edit.anchor() == 4);
    CHECK(edit.caret() == 0);
    CHECK(edit.selectedText() == "WXYZ");
    edit.moveCaret(UiTextMovement::Next);
    CHECK(edit.caret() == 4);
    CHECK(edit.selection().empty());

    edit.setCaret(1);
    edit.setCaret(3, true);
    CHECK(edit.selectedText() == "XY");
    CHECK(edit.insert("abc"));
    CHECK(edit.text() == "WabcZ");
    CHECK(edit.selection() == (UiTextRange { 4, 4 }));
    edit.selectAll();
    CHECK(!edit.insert(edit.selectedText()));
    CHECK(edit.selection().empty());
    edit.selectAll();
    CHECK(edit.eraseSelection());
    CHECK(edit.text().empty());
    CHECK(edit.caret() == 0);
}

void testEditsThatJoinGraphemes()
{
    TEST("editsThatJoinGraphemes");
    UiTextEdit edit;
    edit.setText("\xcc\x81X");
    edit.moveCaret(UiTextMovement::Home);
    CHECK(edit.insert("e"));
    CHECK(edit.text() == std::string(accented) + "X");
    CHECK(edit.caret() == accented.size());
    CHECK(edit.backspace());
    CHECK(edit.text() == "X");

    edit.setText(std::string(regionalA) + "X" + std::string(regionalB));
    edit.setCaret(regionalA.size() + 1);
    CHECK(edit.backspace());
    CHECK(edit.text() == std::string(regionalA) + std::string(regionalB));
    CHECK(edit.caret() == edit.text().size());
    CHECK(edit.backspace());
    CHECK(edit.text().empty());
}

void testSingleLineUtf8InputAndLimits()
{
    TEST("singleLineUtf8InputAndLimits");
    UiTextEdit edit;
    constexpr char pasted[] = "a\r\nb\tc\xe2\x80\xa8" "d\xc2\x85" "e\0f\x7f";
    CHECK(edit.insert(std::string_view(pasted, sizeof(pasted) - 1)));
    CHECK(edit.text() == "a b c d ef");
    edit.selectAll();
    CHECK(edit.insert("q\xc0\xafz"));
    CHECK(edit.text() == "q\xef\xbf\xbd\xef\xbf\xbdz");

    UiTextEdit bounded(4);
    bounded.setText(std::string("A") + std::string(accented) + "B");
    CHECK(bounded.text() == std::string("A") + std::string(accented));
    bounded.setCaret(1);
    bounded.moveCaret(UiTextMovement::End, true);
    CHECK(!bounded.insert(womanTechnologist));
    CHECK(bounded.selectedText() == accented);
    CHECK(bounded.insert("abc"));
    CHECK(bounded.text() == "Aabc");
    CHECK(!bounded.insert("x"));
    UiTextEdit atClusterBoundary(3);
    atClusterBoundary.setText(std::string("A") + std::string(accented));
    CHECK(atClusterBoundary.text() == "A");
}

void testCompositionKeepsCommittedTextAndSelection()
{
    TEST("compositionKeepsCommittedTextAndSelection");
    constexpr std::string_view japanese = "\xe3\x81\x8b";
    constexpr std::string_view mixed = "\xf0\x9f\x98\x80\xe3\x81\x8b";
    UiTextEdit edit;
    edit.setText("ABCD");
    edit.setCaret(1);
    edit.setCaret(3, true);
    edit.setComposition(mixed, 1, 1);
    CHECK(edit.composition().text == mixed);
    CHECK(edit.composition().selection == (UiTextRange { 4, 7 }));
    edit.setComposition(mixed, 2, 1, UiTextOffsetUnit::Utf16);
    CHECK(edit.composition().selection == (UiTextRange { 4, 7 }));
    CHECK(edit.text() == "ABCD");
    CHECK(edit.selectedText() == "BC");
    edit.clearComposition();
    CHECK(edit.composition().text.empty());
    CHECK(edit.selectedText() == "BC");

    edit.setComposition(accented, 1, 1);
    CHECK(edit.composition().selection == (UiTextRange { 0, accented.size() }));
    CHECK(edit.insert(japanese));
    CHECK(edit.composition().text.empty());
    CHECK(edit.text() == std::string("A") + std::string(japanese) + "D");
    CHECK(edit.caret() == 1 + japanese.size());
    edit.setComposition(mixed, std::numeric_limits<int>::max(), 3);
    CHECK(edit.composition().selection == (UiTextRange { mixed.size(), mixed.size() }));
    edit.moveCaret(UiTextMovement::Home);
    CHECK(edit.composition().text.empty());
}

void testMakerTileGuideSearch()
{
    TEST("makerTileGuideSearch");
    using namespace makerTileGuide;
    CHECK(name(TileType::Rogue) == "Paladin");
    CHECK(name(TileType::StreamRight) == "Water Stream");
    CHECK(forTile(TileType::StreamRight).placementHint == forTile(TileType::StreamUp).placementHint);
    CHECK(matches(TileType::LilyPad, "Terrain", " TERRAIN\tfloat WATER "));
    CHECK(matches(TileType::Rogue, "Characters", "rogue push"));
    CHECK(matches(TileType::ConveyorUp, "Mechanisms", "belt carries"));
    CHECK(matches(TileType::Minecart, "Rails", "wagon connected stop"));
    CHECK(!matches(TileType::LilyPad, "Terrain", "water signal"));
    CHECK(matches(TileType::Water, "Terrain", " \t"));
    CHECK(matchesSearchTerms("Horizontal Flip", "flip hor"));
    CHECK(!matchesSearchTerms("Horizontal Flip", "flip hor unrelated"));

    for (const auto& tile : tileTypeDefinitions()) {
        const auto entry = forTile(tile.type);
        CHECK(!entry.description.empty());
        CHECK(!entry.placementHint.empty());
    }
}

void testColorValueFormats()
{
    TEST("colorValueFormats");
    using namespace sokoban::ui::color;
    const Vec3 color { 0.2f, 0.4f, 0.8f };
    CHECK(format(color, Format::Hex) == "#3366CC");
    CHECK(format(color, Format::Rgb) == "51, 102, 204");
    CHECK(format(color, Format::Cmyk) == "75%, 50%, 0%, 20%");
    CHECK(format(color, Format::Hsv) == "220\xc2\xb0, 75%, 80%");
    CHECK(format(color, Format::Hsl) == "220\xc2\xb0, 60%, 50%");
    CHECK(format({}, Format::Cmyk) == "0%, 0%, 0%, 100%");
    CHECK(format({ 1.0f, 1.0f, 1.0f }, Format::Hsl) == "0\xc2\xb0, 0%, 100%");

    CHECK(parse(" 3366cc ", Format::Hex) == color);
    CHECK(parse("#36c", Format::Hex) == color);
    CHECK(parse("51 102 204", Format::Rgb) == color);
    CHECK(parse("51,102 , 204", Format::Rgb) == color);
    CHECK(parse("0 0 0 100", Format::Cmyk) == Vec3 {});
    const Vec3 red { 1.0f, 0.0f, 0.0f };
    CHECK(parse("360deg 100% 100%", Format::Hsv) == red);
    CHECK(parse("360, 100, 50", Format::Hsl) == red);
    CHECK(fromHsv({ -120.0f, 1.0f, 1.0f }) == (Vec3 { 0.0f, 0.0f, 1.0f }));
    const Hsv gray = toHsv({ 0.5f, 0.5f, 0.5f });
    CHECK(gray.hue == 0.0f && gray.saturation == 0.0f && gray.value == 0.5f);
}

void testColorValueRoundTripsPreserveRgbBytes()
{
    TEST("colorValueRoundTripsPreserveRgbBytes");
    using namespace sokoban::ui::color;
    constexpr std::array values { 0, 1, 51, 127, 128, 204, 254, 255 };
    constexpr std::array formats { Format::Hex, Format::Rgb, Format::Cmyk, Format::Hsv, Format::Hsl };
    for (int red : values) {
        for (int green : values) {
            for (int blue : values) {
                const Vec3 rgb { static_cast<float>(red) / 255.0f,
                    static_cast<float>(green) / 255.0f,
                    static_cast<float>(blue) / 255.0f };
                const std::string expected = format(rgb, Format::Hex);
                for (Format mode : formats) {
                    const auto parsed = parse(format(rgb, mode), mode);
                    CHECK(parsed && format(*parsed, Format::Hex) == expected);
                }
            }
        }
    }
}

void testColorValueValidation()
{
    TEST("colorValueValidation");
    using namespace sokoban::ui::color;
    for (const auto text : { "", "#", "#1234", "#001122FF", "#xyz", "0x12ab", "12 345" }) {
        CHECK_MESSAGE(!parse(text, Format::Hex), text);
    }
    for (const auto text : { "1,2", "1,,2,3", "1,2,3,", "1 2 3 4", "256 0 0",
             "-1,0,0", "1,2.5,3", "1%,2,3", "2e2,0,0", "255.000001,0,0",
             "1-0 0", "1,0,nan" }) {
        CHECK_MESSAGE(!parse(text, Format::Rgb), text);
    }
    CHECK(!parse("0, 0, 0, 100.001%", Format::Cmyk));
    CHECK(!parse("0 0 0", Format::Cmyk));
    CHECK(!parse("361, 50%, 50%", Format::Hsv));
    CHECK(!parse("1%, 50%, 50%", Format::Hsv));
    CHECK(!parse("1, 50deg, 50", Format::Hsl));
    CHECK(!parse("1, 50, 101", Format::Hsl));
    CHECK(!parse("1, inf, 0", Format::Hsl));
}

} // namespace

int main()
{
    testGraphemeMovementAndDeletion();
    testSelectionReplacesInEitherDirection();
    testEditsThatJoinGraphemes();
    testSingleLineUtf8InputAndLimits();
    testCompositionKeepsCommittedTextAndSelection();
    testMakerTileGuideSearch();
    testColorValueFormats();
    testColorValueRoundTripsPreserveRgbBytes();
    testColorValueValidation();
    return failures == 0 ? 0 : 1;
}
