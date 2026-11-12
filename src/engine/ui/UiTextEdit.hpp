#pragma once

#include "engine/ui/TextLayout.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

struct UiTextRange {
    std::size_t start = 0;
    std::size_t end = 0;

    [[nodiscard]] bool empty() const { return start == end; }
    friend bool operator==(UiTextRange, UiTextRange) = default;
};

struct UiTextComposition {
    std::string text;
    UiTextRange selection;
};

enum class UiTextMovement : uint8_t { Previous, Next, Home, End };
enum class UiTextOffsetUnit : uint8_t { Codepoints, Utf16 };

// Match every whitespace-delimited term against an English display label.
[[nodiscard]] bool matchesSearchTerms(std::string_view label, std::string_view query);

// Single-line UTF-8 buffer. Caret, anchor, and selection offsets are bytes at
// grapheme boundaries. The byte limit applies to whole graphemes, including
// text inserted through the clipboard.
class UiTextEdit {
public:
    explicit UiTextEdit(std::size_t maxBytes = 4096);

    void setText(std::string_view text);
    [[nodiscard]] const std::string& text() const { return text_; }
    [[nodiscard]] std::size_t caret() const { return caret_; }
    [[nodiscard]] std::size_t anchor() const { return anchor_; }
    [[nodiscard]] UiTextRange selection() const;
    [[nodiscard]] std::string_view selectedText() const;

    void setCaret(std::size_t offset, bool extend = false);
    void moveCaret(UiTextMovement movement, bool extend = false);
    void selectAll();

    // Empty or entirely rejected input leaves a selection intact. The return
    // value says whether committed text changed.
    bool insert(std::string_view text);
    bool backspace();
    bool deleteForward();
    bool eraseSelection();

    // Preedit is shown in place of the current selection until the input
    // method commits text or cancels it.
    void setComposition(
        std::string_view text, int start, int length,
        UiTextOffsetUnit unit = UiTextOffsetUnit::Codepoints);
    void clearComposition();
    [[nodiscard]] const UiTextComposition& composition() const { return composition_; }

private:
    void applyText(std::string text, std::size_t caret);
    bool replace(UiTextRange range, std::string_view replacement);
    [[nodiscard]] std::size_t previousBoundary(std::size_t offset) const;
    [[nodiscard]] std::size_t nextBoundary(std::size_t offset) const;

    std::size_t maxBytes_;
    std::string text_;
    std::vector<TextBoundary> boundaries_;
    std::size_t caret_ = 0;
    std::size_t anchor_ = 0;
    UiTextComposition composition_;
};

} // namespace sokoban
