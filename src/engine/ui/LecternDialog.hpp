#pragma once

#include "engine/ui/Ui.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

class LecternDialog {
public:
    void reset() { page_ = 0; pageCount_ = 1; }
    void turnPage(bool previous, bool next);
    // Preserves paragraphs and splits oversized words so every page fits.
    [[nodiscard]] static std::vector<std::string> wrapText(
        std::string_view text, float width,
        const std::function<float(std::string_view)>& measure);
    // Returns true when the reader clicks Close.
    [[nodiscard]] bool draw(UiContext& ui, Vec2 viewport,
        std::string_view text, std::string_view closeBinding);

private:
    std::size_t page_ = 0;
    std::size_t pageCount_ = 1;
    std::string cachedText_;
    std::vector<std::string> lines_;
    float cachedWidth_ = 0.0f;
    float cachedFontSize_ = 0.0f;
};

} // namespace sokoban
