#include "engine/ui/TextLayout.hpp"

#include <graphemebreak.h>
#include <linebreak.h>

namespace sokoban {

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

} // namespace sokoban
