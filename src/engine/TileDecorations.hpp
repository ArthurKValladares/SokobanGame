#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace sokoban::TileDecorations {

enum class Style : uint8_t { Pebbles, Grass, Moss };
enum class Layout : uint8_t { Edge, Corner, Strip, End };

inline constexpr std::array styles { Style::Pebbles, Style::Grass, Style::Moss };
inline constexpr std::array layouts {
    Layout::Edge, Layout::Corner, Layout::Strip, Layout::End,
};
inline constexpr uint8_t variantCount = 4;

struct Brush {
    Style style = Style::Grass;
    Layout layout = Layout::Edge;
    uint8_t variant = 0;
    uint8_t quarterTurns = 0;
    // In Random mode, variant is the stable preview for the next placement.
    bool randomVariation = false;

    bool operator==(const Brush&) const = default;
};

struct Entry {
    Style style;
    Layout layout;
    uint8_t variant;
    std::string_view model;
};

inline constexpr std::array<Entry, 48> catalog {{
    { Style::Pebbles, Layout::Edge, 0, "TileDecorationPebblesEdge01" },
    { Style::Pebbles, Layout::Edge, 1, "TileDecorationPebblesEdge02" },
    { Style::Pebbles, Layout::Edge, 2, "TileDecorationPebblesEdge03" },
    { Style::Pebbles, Layout::Edge, 3, "TileDecorationPebblesEdge04" },
    { Style::Pebbles, Layout::Corner, 0, "TileDecorationPebblesCorner01" },
    { Style::Pebbles, Layout::Corner, 1, "TileDecorationPebblesCorner02" },
    { Style::Pebbles, Layout::Corner, 2, "TileDecorationPebblesCorner03" },
    { Style::Pebbles, Layout::Corner, 3, "TileDecorationPebblesCorner04" },
    { Style::Pebbles, Layout::Strip, 0, "TileDecorationPebblesStrip01" },
    { Style::Pebbles, Layout::Strip, 1, "TileDecorationPebblesStrip02" },
    { Style::Pebbles, Layout::Strip, 2, "TileDecorationPebblesStrip03" },
    { Style::Pebbles, Layout::Strip, 3, "TileDecorationPebblesStrip04" },
    { Style::Pebbles, Layout::End, 0, "TileDecorationPebblesEnd01" },
    { Style::Pebbles, Layout::End, 1, "TileDecorationPebblesEnd02" },
    { Style::Pebbles, Layout::End, 2, "TileDecorationPebblesEnd03" },
    { Style::Pebbles, Layout::End, 3, "TileDecorationPebblesEnd04" },
    { Style::Grass, Layout::Edge, 0, "TileDecorationGrassEdge01" },
    { Style::Grass, Layout::Edge, 1, "TileDecorationGrassEdge02" },
    { Style::Grass, Layout::Edge, 2, "TileDecorationGrassEdge03" },
    { Style::Grass, Layout::Edge, 3, "TileDecorationGrassEdge04" },
    { Style::Grass, Layout::Corner, 0, "TileDecorationGrassCorner01" },
    { Style::Grass, Layout::Corner, 1, "TileDecorationGrassCorner02" },
    { Style::Grass, Layout::Corner, 2, "TileDecorationGrassCorner03" },
    { Style::Grass, Layout::Corner, 3, "TileDecorationGrassCorner04" },
    { Style::Grass, Layout::Strip, 0, "TileDecorationGrassStrip01" },
    { Style::Grass, Layout::Strip, 1, "TileDecorationGrassStrip02" },
    { Style::Grass, Layout::Strip, 2, "TileDecorationGrassStrip03" },
    { Style::Grass, Layout::Strip, 3, "TileDecorationGrassStrip04" },
    { Style::Grass, Layout::End, 0, "TileDecorationGrassEnd01" },
    { Style::Grass, Layout::End, 1, "TileDecorationGrassEnd02" },
    { Style::Grass, Layout::End, 2, "TileDecorationGrassEnd03" },
    { Style::Grass, Layout::End, 3, "TileDecorationGrassEnd04" },
    { Style::Moss, Layout::Edge, 0, "TileDecorationMossEdge01" },
    { Style::Moss, Layout::Edge, 1, "TileDecorationMossEdge02" },
    { Style::Moss, Layout::Edge, 2, "TileDecorationMossEdge03" },
    { Style::Moss, Layout::Edge, 3, "TileDecorationMossEdge04" },
    { Style::Moss, Layout::Corner, 0, "TileDecorationMossCorner01" },
    { Style::Moss, Layout::Corner, 1, "TileDecorationMossCorner02" },
    { Style::Moss, Layout::Corner, 2, "TileDecorationMossCorner03" },
    { Style::Moss, Layout::Corner, 3, "TileDecorationMossCorner04" },
    { Style::Moss, Layout::Strip, 0, "TileDecorationMossStrip01" },
    { Style::Moss, Layout::Strip, 1, "TileDecorationMossStrip02" },
    { Style::Moss, Layout::Strip, 2, "TileDecorationMossStrip03" },
    { Style::Moss, Layout::Strip, 3, "TileDecorationMossStrip04" },
    { Style::Moss, Layout::End, 0, "TileDecorationMossEnd01" },
    { Style::Moss, Layout::End, 1, "TileDecorationMossEnd02" },
    { Style::Moss, Layout::End, 2, "TileDecorationMossEnd03" },
    { Style::Moss, Layout::End, 3, "TileDecorationMossEnd04" },
}};

[[nodiscard]] constexpr std::string_view styleLabel(Style style)
{
    switch (style) {
    case Style::Pebbles: return "Pebbles";
    case Style::Grass: return "Grass";
    case Style::Moss: return "Moss";
    }
    return "Grass";
}

[[nodiscard]] constexpr std::string_view layoutLabel(Layout layout)
{
    switch (layout) {
    case Layout::Edge: return "One edge";
    case Layout::Corner: return "Two adjacent edges";
    case Layout::Strip: return "Two opposite edges";
    case Layout::End: return "Three edges";
    }
    return "One edge";
}

[[nodiscard]] constexpr Brush normalized(Brush brush)
{
    if (brush.style != Style::Pebbles && brush.style != Style::Grass &&
        brush.style != Style::Moss) {
        brush.style = Style::Grass;
    }
    if (brush.layout != Layout::Edge && brush.layout != Layout::Corner &&
        brush.layout != Layout::Strip && brush.layout != Layout::End) {
        brush.layout = Layout::Edge;
    }
    brush.variant %= variantCount;
    brush.quarterTurns %= 4;
    return brush;
}

[[nodiscard]] constexpr std::string_view modelName(Brush brush)
{
    brush = normalized(brush);
    for (const Entry& entry : catalog) {
        if (entry.style == brush.style && entry.layout == brush.layout &&
            entry.variant == brush.variant) {
            return entry.model;
        }
    }
    return {};
}

[[nodiscard]] constexpr std::optional<Brush> brushForModel(std::string_view model)
{
    for (const Entry& entry : catalog) {
        if (entry.model == model) {
            return Brush { entry.style, entry.layout, entry.variant, 0 };
        }
    }
    return std::nullopt;
}

[[nodiscard]] constexpr bool isTileDecoration(std::string_view model)
{
    return brushForModel(model).has_value();
}

} // namespace sokoban::TileDecorations
