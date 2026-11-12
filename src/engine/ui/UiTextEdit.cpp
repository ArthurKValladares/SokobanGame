#include "engine/ui/UiTextEdit.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <utility>

namespace sokoban {
namespace {

struct Utf8Character {
    char32_t value;
    std::size_t length;
};

constexpr char32_t replacementCharacter = U'\ufffd';
constexpr Utf8Character invalidCharacter { replacementCharacter, 1 };

Utf8Character readUtf8(std::string_view text)
{
    const auto lead = static_cast<unsigned char>(text.front());
    if (lead < 0x80) return { lead, 1 };
    if (lead < 0xc2 || lead > 0xf4) return invalidCharacter;

    const std::size_t length = lead < 0xe0 ? 2U : (lead < 0xf0 ? 3U : 4U);
    if (length > text.size()) return invalidCharacter;

    char32_t value = lead & (0x7fU >> length);
    for (std::size_t index = 1; index < length; ++index) {
        const auto tail = static_cast<unsigned char>(text[index]);
        if ((tail & 0xc0U) != 0x80U) return invalidCharacter;
        value = (value << 6) | (tail & 0x3fU);
    }
    constexpr char32_t minimum[] { 0, 0, 0x80, 0x800, 0x10000 };
    if (value < minimum[length] || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff)) {
        return invalidCharacter;
    }
    return { value, length };
}

bool singleLineSpace(char32_t value)
{
    return value == U'\t' || value == U'\r' || value == U'\n' ||
        value == U'\v' || value == U'\f' || value == 0x85 ||
        (value >= 0x1c && value <= 0x1e) || value == 0x2028 || value == 0x2029;
}

std::size_t boundaryAtOrBefore(
    const std::vector<TextBoundary>& boundaries, std::size_t offset)
{
    const auto next = std::ranges::upper_bound(boundaries, offset, {}, &TextBoundary::end);
    return next == boundaries.begin() ? 0 : std::prev(next)->end;
}

std::size_t boundaryAtOrAfter(
    const std::vector<TextBoundary>& boundaries, std::size_t offset)
{
    if (offset == 0 || boundaries.empty()) return 0;
    const auto next = std::ranges::lower_bound(boundaries, offset, {}, &TextBoundary::end);
    return next == boundaries.end() ? boundaries.back().end : next->end;
}

std::string singleLineText(std::string_view input, std::size_t maxBytes)
{
    if (maxBytes == 0) return {};

    std::string result;
    result.reserve(std::min(input.size(), maxBytes));
    while (!input.empty() && result.size() <= maxBytes) {
        const Utf8Character character = readUtf8(input);
        const auto bytes = input.substr(0, character.length);
        input.remove_prefix(character.length);
        if (singleLineSpace(character.value)) {
            if (character.value == U'\r' && input.starts_with('\n')) {
                input.remove_prefix(1);
            }
            result += ' ';
        } else if (character.value == replacementCharacter && character.length == 1) {
            result += "\xef\xbf\xbd";
        } else if (character.value >= 0x20 &&
            (character.value < 0x7f || character.value >= 0xa0)) {
            result += bytes;
        }
    }
    if (result.size() > maxBytes) {
        // Keeping one scalar beyond the limit lets segmentation tell us if
        // the last bytes that fit are part of a longer grapheme.
        result.resize(boundaryAtOrBefore(textBoundaries(result), maxBytes));
    }
    return result;
}

std::size_t byteOffset(
    std::string_view text, std::size_t units, UiTextOffsetUnit unit)
{
    std::size_t offset = 0;
    while (units > 0 && offset < text.size()) {
        const Utf8Character character = readUtf8(text.substr(offset));
        const std::size_t consumed =
            unit == UiTextOffsetUnit::Utf16 && character.value > 0xffff ? 2U : 1U;
        if (units < consumed) break;
        units -= consumed;
        offset += character.length;
    }
    return offset;
}

} // namespace

bool matchesSearchTerms(std::string_view label, std::string_view query)
{
    const auto caseInsensitive = [](unsigned char left, unsigned char right) {
        return std::tolower(left) == std::tolower(right);
    };
    while (!query.empty()) {
        const auto first = query.find_first_not_of(" \t");
        if (first == std::string_view::npos) {
            break;
        }
        query.remove_prefix(first);
        const std::string_view term = query.substr(0, query.find_first_of(" \t"));
        if (std::ranges::search(label, term, caseInsensitive).empty()) {
            return false;
        }
        query.remove_prefix(term.size());
    }
    return true;
}

UiTextEdit::UiTextEdit(std::size_t maxBytes)
    : maxBytes_(maxBytes)
{
}

void UiTextEdit::setText(std::string_view text)
{
    std::string cleaned = singleLineText(text, maxBytes_);
    const auto end = cleaned.size();
    applyText(std::move(cleaned), end);
}

UiTextRange UiTextEdit::selection() const
{
    return { std::min(caret_, anchor_), std::max(caret_, anchor_) };
}

std::string_view UiTextEdit::selectedText() const
{
    const UiTextRange range = selection();
    return std::string_view(text_).substr(range.start, range.end - range.start);
}

void UiTextEdit::setCaret(std::size_t offset, bool extend)
{
    clearComposition();
    caret_ = boundaryAtOrBefore(boundaries_, offset);
    if (!extend) anchor_ = caret_;
}

void UiTextEdit::moveCaret(UiTextMovement movement, bool extend)
{
    const UiTextRange range = selection();
    switch (movement) {
    case UiTextMovement::Previous:
        setCaret(!extend && !range.empty() ? range.start : previousBoundary(caret_), extend);
        break;
    case UiTextMovement::Next:
        setCaret(!extend && !range.empty() ? range.end : nextBoundary(caret_), extend);
        break;
    case UiTextMovement::Home:
        setCaret(0, extend);
        break;
    case UiTextMovement::End:
        setCaret(text_.size(), extend);
        break;
    }
}

void UiTextEdit::selectAll()
{
    clearComposition();
    anchor_ = 0;
    caret_ = text_.size();
}

bool UiTextEdit::insert(std::string_view text)
{
    const UiTextRange range = selection();
    const std::size_t available = maxBytes_ - (text_.size() - (range.end - range.start));
    const std::string cleaned = singleLineText(text, available);
    clearComposition();
    return !cleaned.empty() && replace(range, cleaned);
}

bool UiTextEdit::backspace()
{
    const UiTextRange range = selection();
    return replace(range.empty() ? UiTextRange { previousBoundary(caret_), caret_ } : range, {});
}

bool UiTextEdit::deleteForward()
{
    const UiTextRange range = selection();
    return replace(range.empty() ? UiTextRange { caret_, nextBoundary(caret_) } : range, {});
}

bool UiTextEdit::eraseSelection()
{
    return replace(selection(), {});
}

void UiTextEdit::setComposition(
    std::string_view text, int start, int length, UiTextOffsetUnit unit)
{
    UiTextComposition next;
    next.text = singleLineText(text, maxBytes_);
    const auto startUnits = static_cast<std::size_t>(std::max(start, 0));
    const auto endUnits = startUnits + static_cast<std::size_t>(std::max(length, 0));
    const std::size_t begin = start >= 0
        ? byteOffset(next.text, startUnits, unit) : next.text.size();
    const std::size_t end = start >= 0 && length > 0
        ? byteOffset(next.text, endUnits, unit) : begin;
    const auto boundaries = textBoundaries(next.text);
    next.selection.start = boundaryAtOrBefore(boundaries, begin);
    next.selection.end = begin == end
        ? next.selection.start : boundaryAtOrAfter(boundaries, end);
    composition_ = std::move(next);
}

void UiTextEdit::clearComposition()
{
    composition_.text.clear();
    composition_.selection = {};
}

void UiTextEdit::applyText(std::string text, std::size_t caret)
{
    auto boundaries = textBoundaries(text);
    text_ = std::move(text);
    boundaries_ = std::move(boundaries);
    caret_ = boundaryAtOrAfter(boundaries_, caret);
    anchor_ = caret_;
    clearComposition();
}

bool UiTextEdit::replace(UiTextRange range, std::string_view replacement)
{
    const std::size_t count = range.end - range.start;
    if (text_.compare(range.start, count, replacement) == 0) {
        setCaret(range.start + replacement.size());
        return false;
    }
    std::string next = text_;
    next.replace(range.start, count, replacement);
    applyText(std::move(next), range.start + replacement.size());
    return true;
}

std::size_t UiTextEdit::previousBoundary(std::size_t offset) const
{
    const auto current = std::ranges::lower_bound(boundaries_, offset, {}, &TextBoundary::end);
    return current == boundaries_.begin() ? 0 : std::prev(current)->end;
}

std::size_t UiTextEdit::nextBoundary(std::size_t offset) const
{
    const auto next = std::ranges::upper_bound(boundaries_, offset, {}, &TextBoundary::end);
    return next == boundaries_.end() ? text_.size() : next->end;
}

} // namespace sokoban
