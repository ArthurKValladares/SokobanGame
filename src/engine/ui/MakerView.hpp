#pragma once

#include "engine/TileTypes.hpp"
#include "engine/ui/Ui.hpp"
#include "engine/ui/UiScroll.hpp"

#include <optional>
#include <string_view>
#include <cstdint>

namespace sokoban {

struct MakerLayout {
    UiRect toolbar;
    UiRect palette;
    UiRect board;
    UiRect footer;
    float scale = 1.0f;
};

struct MakerViewState {
    TileType brush = TileType::Ground;
    uint32_t quarterTurns = 0;
    uint32_t layer = 0;
    uint32_t layerCount = 1;
    bool erasing = false;
    bool canUndo = false;
    bool canRedo = false;
    std::optional<GridPosition3> hovered;
    std::string_view status;
};

struct MakerViewInput {
    float wheelY = 0.0f;
    std::optional<Vec2> wheelPosition;
    UiNavigation navigation;
};

enum class MakerCommand : uint8_t {
    None,
    Back,
    Paint,
    Erase,
    Rotate,
    LayerDown,
    LayerUp,
    Undo,
    Redo,
};

struct MakerViewAction {
    std::optional<TileType> tile;
    MakerCommand command = MakerCommand::None;
};

// Draws the Maker controls and leaves the board viewport open for the scene.
// The same layout rectangle is shared by rendering and board hit testing.
class MakerView {
public:
    [[nodiscard]] static MakerLayout layout(Vec2 viewport, float scale = 1.0f);
    [[nodiscard]] MakerViewAction draw(UiContext& ui, const MakerLayout& layout,
        const MakerViewState& state, const MakerViewInput& input = {});

private:
    UiScroll paletteScroll_;
    UiControlId focus_ = 0;
};

} // namespace sokoban
