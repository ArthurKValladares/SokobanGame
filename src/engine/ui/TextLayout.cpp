#include "engine/ui/TextLayout.hpp"

#include <algorithm>
#include <iterator>

#include <graphemebreak.h>
#include <linebreak.h>

namespace sokoban {

std::optional<TextLineBreak> findTextLineBreak(std::string_view text)
{
    for (std::size_t i = 0; i < text.size(); ++i) {
        switch (static_cast<unsigned char>(text[i])) {
        case '\r':
            return TextLineBreak { i, text.substr(i).starts_with("\r\n") ? 2U : 1U };
        case '\n':
        case '\v':
        case '\f':
        case 0x1c: // File, group, and record separators end bidi paragraphs.
        case 0x1d:
        case 0x1e:
            return TextLineBreak { i, 1 };
        case 0xc2:
            if (text.substr(i).starts_with("\xc2\x85")) return TextLineBreak { i, 2 };
            break;
        case 0xe2:
            if (const auto suffix = text.substr(i);
                suffix.starts_with("\xe2\x80\xa8") || suffix.starts_with("\xe2\x80\xa9")) {
                return TextLineBreak { i, 3 };
            }
            break;
        default:
            break;
        }
    }
    return std::nullopt;
}

std::vector<TextBoundary> textBoundaries(std::string_view text)
{
    std::vector<TextBoundary> result;
    if (text.empty()) return result;
    std::vector<char> graphemes(text.size());
    std::vector<char> lines(text.size());
    const auto* bytes = reinterpret_cast<const utf8_t*>(text.data());
    set_graphemebreaks_utf8(bytes, text.size(), nullptr, graphemes.data());
    set_linebreaks_utf8(bytes, text.size(), nullptr, lines.data());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (graphemes[index] == GRAPHEMEBREAK_BREAK || index + 1 == text.size()) {
            result.push_back({ index + 1,
                lines[index] == LINEBREAK_ALLOWBREAK || lines[index] == LINEBREAK_MUSTBREAK });
        }
    }
    return result;
}

std::vector<std::string> wrapTextLines(
    std::string_view text, float width,
    const std::function<float(std::string_view)>& measure)
{
    std::vector<std::string> lines;
    std::size_t paragraph = 0;
    do {
        const auto separator = findTextLineBreak(text.substr(paragraph));
        std::string_view remaining = text.substr(paragraph,
            separator ? separator->offset : std::string_view::npos);
        if (remaining.empty()) lines.emplace_back();
        const auto fits = [&](std::string_view prefix) { return measure(prefix) <= width; };
        while (!remaining.empty()) {
            const auto boundaries = textBoundaries(remaining);
            // A line ends at the last break opportunity whose prefix fits, so
            // only those prefixes (and the whole remainder) need measuring:
            // about one measurement per word. Measuring every grapheme
            // prefix, as this once did, shaped a whole line for each of its
            // characters. Widths are taken to grow with the prefix.
            std::size_t opportunity = 0;
            bool overflowed = false;
            for (std::size_t index = 0; index < boundaries.size(); ++index) {
                const TextBoundary boundary = boundaries[index];
                if (!boundary.lineBreak && index + 1 != boundaries.size()) continue;
                if (!fits(remaining.substr(0, boundary.end))) {
                    overflowed = true;
                    break;
                }
                if (boundary.lineBreak) opportunity = boundary.end;
            }
            std::size_t count = remaining.size();
            if (overflowed && opportunity) {
                count = opportunity;
            } else if (overflowed) {
                // Not even the first word fits: split it at the last grapheme
                // boundary that does, found by bisection, and always take at
                // least one grapheme so the loop advances.
                const auto split = std::partition_point(boundaries.begin(), boundaries.end(),
                    [&](TextBoundary boundary) { return fits(remaining.substr(0, boundary.end)); });
                count = split == boundaries.begin() ? boundaries.front().end : std::prev(split)->end;
            }
            auto line = remaining.substr(0, count);
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
            lines.emplace_back(line);
            remaining.remove_prefix(count);
            while (!remaining.empty() && (remaining.front() == ' ' || remaining.front() == '\t')) {
                remaining.remove_prefix(1);
            }
        }
        if (!separator) break;
        paragraph += separator->offset + separator->length;
    } while (paragraph <= text.size());
    return lines;
}

} // namespace sokoban
