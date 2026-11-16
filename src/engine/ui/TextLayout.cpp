#include "engine/ui/TextLayout.hpp"

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
        while (!remaining.empty()) {
            const auto boundaries = textBoundaries(remaining);
            std::size_t fit = 0, opportunity = 0;
            for (const TextBoundary boundary : boundaries) {
                if (measure(remaining.substr(0, boundary.end)) > width) break;
                fit = boundary.end;
                if (boundary.lineBreak) opportunity = fit;
            }
            if (!fit) fit = boundaries.front().end;
            const std::size_t count = fit < remaining.size() && opportunity ? opportunity : fit;
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
