#pragma once

#include "engine/Math.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

class FontAtlas;

struct UiRect {
    Vec2 position {};
    Vec2 size {};
};

enum class UiDrawKind {
    Solid,
    FontGlyph,
};

struct UiDrawCommand {
    UiDrawKind kind = UiDrawKind::Solid;
    UiRect rect {};
    UiRect uvRect {};
    Vec4 color {};
};

struct UiDrawData {
    Vec2 viewportSize {};
    std::vector<UiDrawCommand> commands;
};

class UiContext {
public:
    explicit UiContext(const FontAtlas& font);

    void beginFrame(Vec2 viewportSize, Vec2 mousePosition, bool mouseDown, bool mousePressed);
    void endFrame();

    [[nodiscard]] const UiDrawData& drawData() const { return drawData_; }
    [[nodiscard]] Vec2 mousePosition() const { return mousePosition_; }
    [[nodiscard]] bool mouseDown() const { return mouseDown_; }
    [[nodiscard]] bool mousePressed() const { return mousePressed_; }
    [[nodiscard]] bool contains(UiRect rect, Vec2 point) const;
    [[nodiscard]] bool hovered(UiRect rect) const;
    [[nodiscard]] bool clicked(UiRect rect) const;
    [[nodiscard]] bool drag(std::string_view id, UiRect rect);

    void rect(UiRect rect, Vec4 color);
    void panel(UiRect rect);
    void divider(UiRect rect);
    void text(Vec2 position, std::string_view text, Vec4 color, float size = 24.0f);
    [[nodiscard]] Vec2 measureText(std::string_view text, float size = 24.0f) const;
    void centeredText(UiRect rect, std::string_view text, Vec4 color, float size = 24.0f);

private:
    const FontAtlas* font_ = nullptr;
    UiDrawData drawData_ {};
    Vec2 mousePosition_ {};
    bool mouseDown_ = false;
    bool mousePressed_ = false;
    std::string activeControl_;
};

} // namespace sokoban
