#pragma once

#include "engine/Math.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace sokoban {

struct UiRect {
    Vec2 position {};
    Vec2 size {};
};

struct UiDrawCommand {
    UiRect rect {};
    Vec4 color {};
};

struct UiDrawData {
    Vec2 viewportSize {};
    std::vector<UiDrawCommand> commands;
};

class UiContext {
public:
    void beginFrame(Vec2 viewportSize, Vec2 mousePosition, bool mouseDown, bool mousePressed);
    void endFrame();

    [[nodiscard]] const UiDrawData& drawData() const { return drawData_; }
    [[nodiscard]] bool button(std::string_view id, UiRect rect, std::string_view label);
    void panel(UiRect rect);
    void text(Vec2 position, std::string_view text, Vec4 color, float scale = 3.0f);

private:
    [[nodiscard]] bool contains(UiRect rect, Vec2 point) const;
    void rect(UiRect rect, Vec4 color);
    void drawGlyph(Vec2 position, char character, Vec4 color, float scale);

    UiDrawData drawData_ {};
    Vec2 mousePosition_ {};
    bool mouseDown_ = false;
    bool mousePressed_ = false;
};

} // namespace sokoban
