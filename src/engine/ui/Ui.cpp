#include "engine/ui/Ui.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace sokoban {
namespace {

using GlyphRows = std::array<const char*, 7>;

GlyphRows glyphRows(char character)
{
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(character)))) {
    case 'A': return { " ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #" };
    case 'C': return { " ####", "#    ", "#    ", "#    ", "#    ", "#    ", " ####" };
    case 'E': return { "#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####" };
    case 'G': return { " ####", "#    ", "#    ", "#  ##", "#   #", "#   #", " ####" };
    case 'I': return { "#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "#####" };
    case 'L': return { "#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####" };
    case 'M': return { "#   #", "## ##", "# # #", "#   #", "#   #", "#   #", "#   #" };
    case 'N': return { "#   #", "##  #", "# # #", "#  ##", "#   #", "#   #", "#   #" };
    case 'Q': return { " ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #" };
    case 'T': return { "#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  " };
    case 'U': return { "#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### " };
    case '?': return { " ### ", "#   #", "    #", "   # ", "  #  ", "     ", "  #  " };
    default: return { "     ", "     ", "     ", "     ", "     ", "     ", "     " };
    }
}

} // namespace

void UiContext::beginFrame(Vec2 viewportSize, Vec2 mousePosition, bool mouseDown, bool mousePressed)
{
    drawData_.viewportSize = viewportSize;
    drawData_.commands.clear();
    mousePosition_ = mousePosition;
    mouseDown_ = mouseDown;
    mousePressed_ = mousePressed;
}

void UiContext::endFrame()
{
}

bool UiContext::button(std::string_view, UiRect rect, std::string_view label)
{
    const bool hovered = contains(rect, mousePosition_);
    const Vec4 color = hovered
        ? (mouseDown_ ? Vec4 { 0.82f, 0.46f, 0.20f, 0.96f } : Vec4 { 0.74f, 0.38f, 0.16f, 0.94f })
        : Vec4 { 0.50f, 0.22f, 0.10f, 0.92f };

    this->rect(rect, { 0.09f, 0.07f, 0.06f, 0.96f });
    this->rect({ { rect.position.x + 2.0f, rect.position.y + 2.0f }, { rect.size.x - 4.0f, rect.size.y - 4.0f } }, color);

    constexpr float textScale = 3.0f;
    const float textWidth = static_cast<float>(label.size()) * 6.0f * textScale;
    const Vec2 textPosition {
        rect.position.x + std::max((rect.size.x - textWidth) * 0.5f, 0.0f),
        rect.position.y + (rect.size.y - 7.0f * textScale) * 0.5f,
    };
    text(textPosition, label, { 0.96f, 0.92f, 0.84f, 1.0f }, textScale);

    return hovered && mousePressed_;
}

void UiContext::panel(UiRect rect)
{
    this->rect(rect, { 0.04f, 0.035f, 0.03f, 0.90f });
    this->rect({ { rect.position.x + 4.0f, rect.position.y + 4.0f }, { rect.size.x - 8.0f, rect.size.y - 8.0f } }, { 0.18f, 0.14f, 0.11f, 0.96f });
}

void UiContext::text(Vec2 position, std::string_view textValue, Vec4 color, float scale)
{
    Vec2 cursor = position;
    for (char character : textValue) {
        if (character == ' ') {
            cursor.x += 6.0f * scale;
            continue;
        }

        drawGlyph(cursor, character, color, scale);
        cursor.x += 6.0f * scale;
    }
}

bool UiContext::contains(UiRect rectValue, Vec2 point) const
{
    return point.x >= rectValue.position.x &&
        point.y >= rectValue.position.y &&
        point.x < rectValue.position.x + rectValue.size.x &&
        point.y < rectValue.position.y + rectValue.size.y;
}

void UiContext::rect(UiRect rectValue, Vec4 color)
{
    if (rectValue.size.x <= 0.0f || rectValue.size.y <= 0.0f || color.w <= 0.0f) {
        return;
    }

    drawData_.commands.push_back({
        .rect = rectValue,
        .color = color,
    });
}

void UiContext::drawGlyph(Vec2 position, char character, Vec4 color, float scale)
{
    const GlyphRows rows = glyphRows(character);
    for (size_t y = 0; y < rows.size(); ++y) {
        for (size_t x = 0; rows[y][x] != '\0'; ++x) {
            if (rows[y][x] == ' ') {
                continue;
            }

            rect({
                { position.x + static_cast<float>(x) * scale, position.y + static_cast<float>(y) * scale },
                { scale, scale },
            }, color);
        }
    }
}

} // namespace sokoban
