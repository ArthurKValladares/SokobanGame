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
    { "Cycle tools", "Cycle through Paint, Erase, Connect, Area, and Flood Fill. In the overworld, Order follows Connect.", InputAction::EditorCycleTool },
    { "Recent tiles", "Recall a brush from one of nine recent slots.", {}, {}, true },
};

inline constexpr Row layerRows[] {
    { "Layers panel", "Open Layers below the palette to select a layer or insert one above or below it. Deleting a layer with content asks for confirmation. In an overworld, layer changes apply to the whole map." },
    { "Water layer", "Select a layer in Layers, then choose Set Water Layer. You can move the water to another layer or clear it." },
    { "Layer up", "Move the active layer up. The toolbar's plus button does the same.", InputAction::EditorLayerUp },
    { "Layer down", "Move the active layer down. The toolbar's minus button does the same.", InputAction::EditorLayerDown },
    { "Lock to layer", "Show and edit only the active layer. Press again to show all layers.",
        InputAction::EditorToggleLayerLock },
    { "Undo", "Undo the last edit. Moving a tile or dragging across the board counts as one edit.", InputAction::Undo },
    { "Redo", "Reapply the last edit you undid.", InputAction::EditorRedo },
};

inline constexpr Row propertyRows[] {
    { "Inspect a tile", "Open Properties below the palette and click a placed tile to see its settings." },
    { "Gates and lock plates", "Choose whether a gate starts open or closed, or whether a lock plate starts enabled or disabled." },
    { "Minecarts", "Choose an available launch direction and read the route summary." },
    { "Elevator stops", "Select an elevator, switch to a layer, then add or remove the stop in Properties." },
};

inline constexpr Row areaRows[] {
    { "Rectangle select", "Choose Area and drag over the cells you want on the active layer. In an overworld, work inside the current screen.",
        InputAction::MakerSelectArea },
    { "Fill selection", "Choose a palette tile, then click Fill to paint the selected rectangle." },
    { "Flood fill", "Click Flood Fill, then a cell to fill adjacent matching tiles on the active layer.",
        InputAction::MakerFloodFill },
    { "Copy", "Copy the rectangle to the Maker clipboard.", InputAction::MakerCopySelection },
    { "Cut", "Copy the rectangle, then erase it in one undoable edit.", InputAction::MakerCutSelection },
    { "Paste", "Preview the copied area on the active layer and click to place it. Keep clicking to stamp more copies.",
        InputAction::MakerPasteArea },
    { "Erase selection", "Erase the tiles in the selected rectangle in one edit.", InputAction::MakerEraseSelection },
    { "Cancel or deselect", "Cancel a paste preview or clear the current selection.", {}, "Esc" },
};

inline constexpr Row viewportRows[] {
    { "Pan", "Drag the board to move the view.", {}, "Middle mouse" },
    { "Zoom", "Scroll over the board to zoom in or out.", {}, "Mouse wheel" },
    { "Top down", "Switch between the overhead view and the screen's camera angle.", InputAction::ShowTopDownView },
    { "Frame selection", "Fit the selected area or inspected tile in view. Frame Board fits the full board.",
        InputAction::MakerFrameSelection },
    { "Board cursor", "Focus the board to edit without a mouse. Use the arrow keys, D-pad, or left stick to move. Press Escape or toggle Board Cursor to return to the controls.",
        InputAction::MakerBoardFocus },
    { "Use the tool", "Confirm or press Enter to apply the selected tool. In Area mode, confirm each corner to select a rectangle.",
        InputAction::MenuConfirm },
    { "Open item at cursor", "Open a Level Selector or lectern at the board cursor.", InputAction::MakerBoardContext },
};

inline constexpr Row connectionRows[] {
    { "Wire a device", "Choose Connect, click a device such as a gate, then click a signal source such as a pressure plate." },
    { "Pair portals", "In Connect, click two unpaired portals. In an overworld, the other portal can be on a visible neighboring screen." },
    { "Synchronize objects", "In Connect, click two movable objects, such as stones, to put them in one sync group." },
    { "Inspect links", "Select a connected tile to highlight its links and manage the group from the inspector." },
    { "Group colors", "Choose Color to preview a new group color on the board. Drag the picker aside for a better view, then choose Apply." },
    { "Elevator stops", "Select an elevator in Connect, switch to the desired layer, then use Add Stop or Remove Stop." },
};

inline constexpr Row draftRows[] {
    { "Problems", "Open Problems below the palette to check player starts, portal pairs, and Level Selector links. Select an issue to jump to its tile or screen." },
    { "New Draft", "Choose the board's width and height to start a fresh draft." },
    { "Resize Board", "Blue shows added cells. Red shows cells that will be removed. Confirm a resize that cuts off tiles." },
    { "Screen Camera", "Use Camera to preview tilt and rotation, then apply the view to this draft." },
    { "Detached Camera", "Choose Detach Camera and click the board to look around freely. Press Esc to release the mouse." },
    { "Save", "Name your draft so you can reopen it later with Load.", InputAction::EditorSave },
    { "Load", "Choose a saved draft. Both New Draft and Load ask before discarding unsaved edits." },
    { "Rename a level", "In Puzzle Maker, open Load, choose a saved level, then select Rename. Level Selectors keep their links." },
    { "Recover an autosave", "Maker saves working edits as you go. When it finds an autosave on entry, choose Recover to reopen the board, then use Save to keep the edits." },
    { "Test Play", "Play the current draft. Return to Maker or press the same shortcut to resume with your edits and undo history.",
        InputAction::EditorPlayDraft },
};

inline constexpr Row overworldRows[] {
    { "Switch editors", "Use Overworld in the palette header to edit an overworld. Use Puzzle to switch back to puzzle drafts." },
    { "Grow the map", "Paint beyond the active screen's edge to create a neighboring screen with the tile you painted and air everywhere else. You can also paint on visible neighboring screens." },
    { "Show Neighbors", "Neighbors are visible by default. Turn them off to focus on one screen. Use the arrows in the footer to browse screens." },
    { "Overworld map", "Open Map in the footer and click a screen to jump to it. Use Move and click an empty slot, or choose Delete and confirm. Undo and Redo work from the map." },
    { "Lorekeeper", "The overworld has one player start. Choose Lorekeeper in Characters to place it." },
    { "Overworld tiles", "Wardrobes change the active character. Right-click a lectern to edit its message." },
    { "Level Selectors", "Place a Level Selector on a path, click it, and pick a saved puzzle. Search by level name to narrow the list." },
    { "Unlock order", "Choose Order. Click the prerequisite first, then the level it unlocks. Click the same destination again to remove the connection. A level with several prerequisites unlocks when all are complete." },
};

inline constexpr Section sections[] {
    { "Editing the board", boardRows },
    { "Viewport", viewportRows },
    { "Area tools", areaRows },
    { "Layers and history", layerRows },
    { "Tile properties", propertyRows },
    { "Connections", connectionRows },
    { "Drafts and test play", draftRows },
    { "Overworld editing", overworldRows },
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
