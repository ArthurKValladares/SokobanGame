#pragma once

#include "engine/InputBindings.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace sokoban::makerHelp {

struct Row {
    std::string_view action;
    std::string_view detail;
    std::optional<InputAction> shortcut {};
    std::string_view fixedShortcut {};
    bool usesRecentSlots = false;
};

struct Section {
    std::string_view title;
    std::span<const Row> rows;
};

inline constexpr Row boardRows[] {
    { "Find a tile", "Search the palette and click a group heading to expand or collapse it. The tile guide shows placement rules.",
        {}, "Ctrl+F" },
    { "Paint", "Choose a tile and click the board to place it on the active layer. Drag to paint multiple cells." },
    { "Rotate", "Press to turn the selected tile brush clockwise before placing it.", InputAction::EditorRotateTile },
    { "Move", "Hold the key and click a tile, then its destination. Release the key to cancel a pending move.",
        InputAction::EditorMoveTile },
    { "Delete", "Hold the key while clicking or dragging on the board. The Erase tool clears the active layer.",
        InputAction::EditorDeleteTile },
    { "Replace", "Hold the key while clicking or dragging to paint on the tile's layer.",
        InputAction::EditorReplaceTile },
    { "Pick up a tile", "Hold the key and click a tile to use it as your brush.", InputAction::EditorPickTile },
    { "Draw a straight line", "Hold the key while dragging to stay on the starting row or column.",
        InputAction::EditorStraightLine },
    { "Cycle tools", "Switch between Paint, Erase, and Connect.", InputAction::EditorCycleTool },
    { "Recent tiles", "Recall a brush from one of nine recent slots.", {}, {}, true },
};

inline constexpr Row layerRows[] {
    { "Layer up", "Move the active layer up. The toolbar's plus button does the same.", InputAction::EditorLayerUp },
    { "Layer down", "Move the active layer down. The toolbar's minus button does the same.", InputAction::EditorLayerDown },
    { "Lock to layer", "Show and edit only the active layer. Press again to show all layers.",
        InputAction::EditorToggleLayerLock },
    { "Undo", "Undo the last edit. Moving a tile or dragging across the board counts as one edit.", InputAction::Undo },
    { "Redo", "Reapply the last edit you undid.", InputAction::EditorRedo },
};

inline constexpr Row connectionRows[] {
    { "Wire a device", "Choose Connect, click a device such as a gate, then click a signal source such as a pressure plate." },
    { "Pair portals", "In Connect, click two unpaired portals to link them." },
    { "Synchronize objects", "In Connect, click two movable objects, such as stones, to put them in one sync group." },
    { "Inspect links", "Select a connected tile to highlight its links and manage the group from the inspector." },
    { "Group colors", "Choose Color to preview a new group color on the board. Drag the picker aside for a better view, then choose Apply." },
    { "Elevator stops", "Select an elevator in Connect, switch to the desired layer, then use Add Stop or Remove Stop." },
};

inline constexpr Row draftRows[] {
    { "New Draft", "Choose the board's width and height to start a fresh draft." },
    { "Resize Board", "Blue shows added cells. Red shows cells that will be removed. Confirm a resize that cuts off tiles." },
    { "Screen Camera", "Use Camera to preview tilt and rotation, then apply the view to this draft." },
    { "Detached Camera", "Choose Detach Camera and click the board to look around freely. Press Esc to release the mouse." },
    { "Save", "Name your draft so you can reopen it later with Load.", InputAction::EditorSave },
    { "Load", "Choose a saved draft. Both New Draft and Load ask before discarding unsaved edits." },
    { "Test Play", "Play the current draft. Return to Maker or press the same shortcut to resume with your edits and undo history.",
        InputAction::EditorPlayDraft },
};

inline constexpr Section sections[] {
    { "Editing the board", boardRows },
    { "Layers and history", layerRows },
    { "Connections", connectionRows },
    { "Drafts and test play", draftRows },
};

[[nodiscard]] inline std::string shortcutLabel(const Row& row, const InputBindings& bindings)
{
    if (row.usesRecentSlots) {
        std::string labels;
        bool usesDefaultKeys = true;
        for (int slot = 0; slot < editorRecentTileActionCount; ++slot) {
            const auto label = actionBindingsDisplay(bindings, editorRecentTileAction(slot));
            usesDefaultKeys = usesDefaultKeys && label == std::to_string(slot + 1);
            if (slot != 0) labels += ", ";
            labels += label;
        }
        return usesDefaultKeys ? "1\xe2\x80\x93" "9" : labels;
    }
    return row.shortcut ? actionBindingsDisplay(bindings, *row.shortcut) : std::string(row.fixedShortcut);
}

} // namespace sokoban::makerHelp
