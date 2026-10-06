#include "engine/LevelEditorDebugUi.hpp"

#include "engine/EditorTilePalette.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/Rules.hpp"
#include "engine/Profiler.hpp"
#include "engine/TileTypes.hpp"
#include "engine/render/RenderTypes.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <system_error>
#include <string_view>
#include <utility>
#include <vector>

#ifndef SOKOBAN_ENABLE_DEBUG_UI
// Deliberately fatal rather than defaulting to 0. This flag decides whether
// Application and DebugUi declare some of their members, so a translation unit
// that quietly assumed a value would disagree with the rest of the program
// about those class layouts - and link anyway. CMake defines it PUBLIC on
// sokoban_core, so anything linking a Sokoban library already has it.
#error "SOKOBAN_ENABLE_DEBUG_UI must be defined by the build (see CMakeLists.txt)"
#endif

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#include <imgui_stdlib.h>
#endif

namespace sokoban {
namespace {

#if SOKOBAN_ENABLE_DEBUG_UI
// Large enough to actually read a model in. Baked thumbnails come out around
// 330px square, so there is plenty of detail to enlarge into.
constexpr ImVec2 paletteButtonSize { 93.6f, 83.2f };

// Laying the row out is the caller's job: at this size the palette no longer
// fits on one line and has to wrap.
bool drawPaintButton(
    const TileTypeDefinition& definition,
    TileType selectedTile,
    ImTextureID thumbnail,
    const editorTilePalette::Group* group = nullptr)
{
    const bool selected = group != nullptr
        ? group->contains(selectedTile)
        : selectedTile == definition.type;
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.50f, 0.85f, 1.0f));
    }

    ImGui::PushID(static_cast<int>(definition.type));
    const bool clicked = ImGui::Button("##paint_tile", paletteButtonSize);
    const ImVec2 buttonMin = ImGui::GetItemRectMin();
    const ImVec2 buttonMax = ImGui::GetItemRectMax();
    const Vec4 color = tileColor(definition.type);
    const float insetX = paletteButtonSize.x * 0.22f;
    const float insetY = paletteButtonSize.y * 0.20f;
    const ImVec2 swatchMin { buttonMin.x + insetX, buttonMin.y + insetY };
    const ImVec2 swatchMax { buttonMax.x - insetX, buttonMax.y - insetY };
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (thumbnail != 0) {
        // A rendered preview of the real asset, drawn over the whole button
        // rather than the swatch inset because a model needs the room, and
        // with straight alpha so only the silhouette shows.
        //
        // The thumbnail is square and the button is not, so it is centred at
        // its own aspect rather than stretched to fill.
        const float side = std::min(
            paletteButtonSize.x, paletteButtonSize.y) - 2.0f;
        const ImVec2 centre {
            (buttonMin.x + buttonMax.x) * 0.5f,
            (buttonMin.y + buttonMax.y) * 0.5f,
        };
        drawList->AddImage(
            thumbnail,
            ImVec2(centre.x - side * 0.5f, centre.y - side * 0.5f),
            ImVec2(centre.x + side * 0.5f, centre.y + side * 0.5f));
    } else {
        // Fall back to a colour swatch while a thumbnail is unavailable.
        drawList->AddRectFilled(
            swatchMin,
            swatchMax,
            ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, color.w)),
            2.0f);
        drawList->AddRect(
            swatchMin,
            swatchMax,
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.55f)),
            2.0f);
        if (definition.type == TileType::Air) {
            const ImU32 airLine = ImGui::ColorConvertFloat4ToU32(ImVec4(0.62f, 0.68f, 0.76f, 0.9f));
            drawList->AddLine(swatchMin, swatchMax, airLine, 1.5f);
            drawList->AddLine(
                ImVec2 { swatchMin.x, swatchMax.y },
                ImVec2 { swatchMax.x, swatchMin.y },
                airLine,
                1.5f);
        }
    }
    if (group != nullptr) {
        drawList->AddText(
            ImVec2(buttonMax.x - 20.0f, buttonMin.y + 2.0f),
            ImGui::GetColorU32(ImGuiCol_Text), "...");
    }
    if (ImGui::IsItemHovered()) {
        if (group != nullptr) {
            ImGui::SetTooltip("%.*s: choose direction\n%.*s",
                static_cast<int>(group->name.size()), group->name.data(),
                static_cast<int>(definition.name.size()), definition.name.data());
        } else {
            ImGui::SetTooltip("%.*s", static_cast<int>(definition.name.size()), definition.name.data());
        }
    }
    ImGui::PopID();

    if (selected) {
        ImGui::PopStyleColor();
    }
    return clicked;
}

bool containsInsensitive(std::string_view value, std::string_view filter)
{
    if (filter.empty()) {
        return true;
    }
    std::string loweredValue(value);
    std::string loweredFilter(filter);
    auto lower = [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    };
    std::ranges::transform(loweredValue, loweredValue.begin(), lower);
    std::ranges::transform(loweredFilter, loweredFilter.begin(), lower);
    return loweredValue.find(loweredFilter) != std::string::npos;
}
#endif

} // namespace

void LevelEditorDebugUi::initialize(const LevelEditor& editor)
{
    syncDocumentPath(editor);
    browserRootBuffer_ = editor.browserRoot().string();
    requestedWidth_ = editor.requestedWidth();
    requestedHeight_ = editor.requestedHeight();
    overworldEditorRoot_ = editor.sourceLevelRoot();
    selectedToolTab_.reset();
}

void LevelEditorDebugUi::draw(
    LevelEditor& editor,
    OverworldMapEditor& overworldEditor,
    SplatPainter& painter,
    const InputBindings& bindings,
    const Callbacks& callbacks)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    SOKOBAN_PROFILE_SCOPE("Editor.Draw panel");
    ImGui::Text("Document");
    ImGui::SameLine();
    ImGui::TextUnformatted(editor.dirty() ? "modified" : "clean");
    ImGui::InputText("Path", &filePathBuffer_);

    if (ImGui::Button("Load")) {
        if (editor.openDocument(filePathBuffer_)) {
            syncDocumentPath(editor);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        if (editor.saveDocument(filePathBuffer_)) {
            syncDocumentPath(editor);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "%s saves to the file the document came from.",
            actionBindingsDisplay(bindings, InputAction::EditorSave).c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Play Draft")) {
        if (std::optional<Level> level =
                editor.beginDraftPlayback(&overworldEditor);
            level && callbacks.playDraft) {
            callbacks.playDraft(std::move(*level));
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "%s plays the draft and returns here.\n"
            "%s plays with the hero moved to the cursor.",
            actionBindingsDisplay(
                bindings, InputAction::EditorPlayDraft).c_str(),
            actionBindingsDisplay(
                bindings, InputAction::EditorPlayFromCursor).c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Return To Current Screen")) {
        editor.setEditingDocument(false);
        if (callbacks.returnToCurrentScreen) {
            callbacks.returnToCurrentScreen();
        }
    }

    ImGui::BeginDisabled(!editor.canUndo());
    if (ImGui::Button("Undo")) {
        (void)editor.tryUndoEdit();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!editor.canRedo());
    if (ImGui::Button("Redo")) {
        (void)editor.tryRedoEdit();
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    drawFileBrowser(editor, overworldEditor);

    ImGui::Separator();
    ImGui::Text("View: %s", editor.editingDocument() ? "editing draft" : editor.playingDraft() ? "playing draft" : "current screen");
    ImGui::Checkbox("Show Debug View", &showDebugView_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Shared-color outlines and dotted links; elevator stops and cart cycles; "
            "turret sightlines, conveyor directions and configurable-block coordinates.\n"
            "Arrows show travel direction; circles mark stops. Hidden layers follow the layer lock.");
    }
    ImGui::BeginDisabled(editor.editingOverworld());
    const bool widthChanged = ImGui::InputInt("Width", &requestedWidth_);
    const bool heightChanged = ImGui::InputInt("Height", &requestedHeight_);
    if (widthChanged || heightChanged) {
        editor.setRequestedSize(requestedWidth_, requestedHeight_);
        requestedWidth_ = editor.requestedWidth();
        requestedHeight_ = editor.requestedHeight();
    }
    if (ImGui::Button("New")) {
        editor.newDocument(requestedWidth_, requestedHeight_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Resize")) {
        editor.resizeDocument(requestedWidth_, requestedHeight_);
    }
    ImGui::EndDisabled();
    if (editor.editingOverworld()) {
        ImGui::TextDisabled(
            "Screen size is fixed by the active overworld layout.");
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader(
            "Screen Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        CameraAngles angles = editor.cameraAngles().value_or(CameraAngles {});
        const auto editAngle = [&] (
            const char* label, float& value, float minimum, float maximum) {
            const bool changed = ImGui::SliderFloat(
                label, &value, minimum, maximum, "%.1f deg",
                ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemActivated()) {
                (void)editor.beginStroke();
            }
            if (changed) {
                editor.setCameraAngles(angles);
            }
            if (ImGui::IsItemDeactivated()) {
                editor.endStroke();
            }
        };
        editAngle(
            "Tilt", angles.pitchDegrees, 0.0f, CameraAngles::maximumPitchDegrees);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "0 degrees looks straight down; larger angles lower the camera.");
        }
        editAngle("Rotation", angles.yawDegrees, -180.0f, 180.0f);
        ImGui::BeginDisabled(!editor.cameraAngles());
        if (ImGui::Button("Reset Camera To Default")) {
            editor.endStroke();
            editor.setCameraAngles(std::nullopt);
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Saved with this screen. Preview updates live.");
    }

    ImGui::Separator();
    ImGui::Text("Layer %d of %d", static_cast<int>(editor.activeLayer()) + 1, static_cast<int>(editor.documentDepth()));
    int selectedLayer = static_cast<int>(editor.activeLayer());
    if (ImGui::SliderInt("Current Layer", &selectedLayer, 0, std::max(static_cast<int>(editor.documentDepth()) - 1, 0))) {
        editor.setActiveLayer(selectedLayer);
    }
    bool waterOnCurrentLayer =
        editor.waterLayer() == editor.activeLayer();
    if (ImGui::Checkbox("Water On This Layer", &waterOnCurrentLayer)) {
        editor.setWaterLayer(
            waterOnCurrentLayer
                ? std::optional<uint32_t>(editor.activeLayer())
                : std::nullopt);
    }
    if (editor.waterLayer() &&
        editor.waterLayer() != editor.activeLayer()) {
        ImGui::TextDisabled(
            "Water is on layer %d.",
            static_cast<int>(*editor.waterLayer()) + 1);
    }
    bool layerLocked = editor.layerLocked();
    if (ImGui::Checkbox("Lock Edits To Current Layer", &layerLocked)) {
        editor.setLayerLocked(layerLocked);
    }
    if (ImGui::Button("+ Layer Below")) {
        editor.addLayerBelow();
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Layer Above")) {
        editor.addLayerAbove();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Layer")) {
        editor.deleteActiveLayer();
    }

    if (editor.editingOverworld() &&
        editor.tool() != LevelEditor::Tool::Selectors &&
        ImGui::Button("Assign Screens To Flags...")) {
        editor.setTool(LevelEditor::Tool::Selectors);
    }
    const LevelEditor::Tool requestedTool = editor.tool();
    const bool selectRequestedTool =
        !selectedToolTab_ || *selectedToolTab_ != requestedTool;
    if (selectRequestedTool) {
        selectedToolTab_ = requestedTool;
    }
    const auto toolTabFlags = [&](LevelEditor::Tool tool) {
        return selectRequestedTool && requestedTool == tool
            ? ImGuiTabItemFlags_SetSelected
            : ImGuiTabItemFlags_None;
    };
    const auto activateToolTab = [&](LevelEditor::Tool tool) {
        // While a programmatic selection is queued, ImGui can expose the old
        // tab's contents for one frame. Do not let that stale tab overwrite
        // the requested tool before the new selection becomes visible.
        if (!selectRequestedTool || requestedTool == tool) {
            selectedToolTab_ = tool;
            if (editor.tool() != tool) {
                editor.setTool(tool);
            }
        }
    };
    if (ImGui::BeginTabBar("LevelEditorToolTabs")) {
        if (ImGui::BeginTabItem(
                "Tiles",
                nullptr,
                toolTabFlags(LevelEditor::Tool::Tiles))) {
            activateToolTab(LevelEditor::Tool::Tiles);
            drawTilePalette(editor, bindings, callbacks);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(
                "Mesh Decorations",
                nullptr,
                toolTabFlags(LevelEditor::Tool::Decorations))) {
            activateToolTab(LevelEditor::Tool::Decorations);
            drawDecorationPalette(editor, callbacks);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(
                "Screen Selectors",
                nullptr,
                toolTabFlags(LevelEditor::Tool::Selectors))) {
            activateToolTab(LevelEditor::Tool::Selectors);
            drawSelectorPalette(editor);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Separator();
    drawGroundPaintTab(editor, painter, callbacks);

    if (!editor.status().empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", editor.status().c_str());
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader(
            "Tile Editing Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        SOKOBAN_PROFILE_SCOPE("Editor.Draw editing controls");
        ImGui::BulletText("Click: paint above the resolved tile");
        ImGui::BulletText(
            "%s + click: replace the resolved tile",
            actionBindingsDisplay(
                bindings, InputAction::EditorReplaceTile).c_str());
        ImGui::BulletText(
            "%s + click twice: select and move any tile object, including flags",
            actionBindingsDisplay(
                bindings, InputAction::EditorMoveTile).c_str());
        ImGui::BulletText(
            "%s + click: delete the resolved tile",
            actionBindingsDisplay(
                bindings, InputAction::EditorDeleteTile).c_str());
        ImGui::BulletText(
            "%s: undo the latest editor change",
            actionBindingsDisplay(bindings, InputAction::Undo).c_str());
        const auto shortcut = [&](InputAction action, const char* what) {
            ImGui::BulletText(
                "%s: %s", actionBindingsDisplay(bindings, action).c_str(), what);
        };
        ImGui::BulletText(
            "Drag: paint (or delete) every tile the pointer crosses; "
            "the whole drag is one undo step");
        ImGui::BulletText(
            "%s + drag: stay on the starting row or column",
            actionBindingsDisplay(
                bindings, InputAction::EditorStraightLine).c_str());
        ImGui::BulletText(
            "%s + click: eyedropper (the cursor turns into one) - pick up "
            "the tile under the pointer, and the link color of a pressure "
            "plate, gate, rotator, lock plate or elevator",
            actionBindingsDisplay(
                bindings, InputAction::EditorPickTile).c_str());
        ImGui::BulletText(
            "%s + click or drag: link-color brush (the cursor turns into a "
            "brush dipped in the link color) - give each pressure plate, "
            "gate, rotator, lock plate or elevator touched the active link color; the "
            "whole drag is one undo step",
            actionBindingsDisplay(
                bindings, InputAction::EditorPaintLinkColor).c_str());
        ImGui::BulletText(
            "While either is held it is the only thing a click does, in any "
            "tool: nothing is placed, deleted, moved or ground-painted, and "
            "the tile preview is hidden");
        shortcut(InputAction::EditorRedo, "redo");
        shortcut(
            InputAction::EditorSave,
            "save the document (or the ground paint)");
        shortcut(
            InputAction::EditorPlayDraft,
            "play the draft, and again to return");
        shortcut(
            InputAction::EditorPlayFromCursor,
            "play with the hero moved to the pointer");
        shortcut(InputAction::EditorLayerUp, "next layer up");
        shortcut(InputAction::EditorLayerDown, "next layer down");
        shortcut(
            InputAction::EditorToggleLayerLock, "lock edits to the layer");
        shortcut(InputAction::EditorCycleTool, "next tool");
        ImGui::BulletText(
            "%s / %s / %s: gizmo move / rotate / scale",
            actionBindingsDisplay(
                bindings, InputAction::EditorGizmoTranslate).c_str(),
            actionBindingsDisplay(
                bindings, InputAction::EditorGizmoRotate).c_str(),
            actionBindingsDisplay(
                bindings, InputAction::EditorGizmoScale).c_str());
        ImGui::BulletText(
            "%s ... %s: recent tiles",
            actionBindingsDisplay(bindings, editorRecentTileAction(0))
                .c_str(),
            actionBindingsDisplay(
                bindings,
                editorRecentTileAction(editorRecentTileActionCount - 1))
                .c_str());
        ImGui::TextDisabled(
            "Rebind these under Options > Controls > Editor Controls. "
            "Click the game view first if a panel has keyboard focus.");
    }
#else
    (void)editor;
    (void)overworldEditor;
    (void)painter;
    (void)bindings;
    (void)callbacks;
#endif
}

#if SOKOBAN_ENABLE_DEBUG_UI
namespace {

// A slider for quick adjustment plus a box for typing an exact value.
//
// `sliderMaximum` is the comfortable range to drag within; `hardMaximum` is
// the real limit typing may reach, so the slider can stay usefully fine
// without capping what can be entered. Values are only clamped once the box
// is no longer being edited, otherwise clamping would fight the user
// mid-keystroke (typing "0.5" passes through "0").
void drawBrushValue(
    const char* label,
    float& value,
    float minimum,
    float sliderMaximum,
    float hardMaximum)
{
    constexpr float inputWidth = 78.0f;
    ImGui::PushID(label);

    const float available = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(std::max(available * 0.45f, 60.0f));
    ImGui::SliderFloat("##slider", &value, minimum, sliderMaximum, "%.3f");
    const bool sliderActive = ImGui::IsItemActive();

    ImGui::SameLine();
    ImGui::SetNextItemWidth(inputWidth);
    ImGui::InputFloat("##input", &value, 0.0f, 0.0f, "%.3f");
    const bool inputActive = ImGui::IsItemActive();

    ImGui::SameLine();
    ImGui::TextUnformatted(label);

    if (!sliderActive && !inputActive) {
        value = std::clamp(value, minimum, hardMaximum);
    }
    ImGui::PopID();
}

} // namespace
#endif

void LevelEditorDebugUi::drawGroundPaintTab(
    LevelEditor& editor, SplatPainter& painter, const Callbacks& callbacks)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::Text("Ground Paint");

    if (ImGui::Button("Add Splat Map") && callbacks.createGroundSplatMap) {
        (void)callbacks.createGroundSplatMap();
    }
    ImGui::TextWrapped("Each map blends its base and detail textures. Choose its color, then paint ground tiles to assign them. The first map is the default.");
    const Level::GroundSplat* selected = editor.selectedGroundSplat();
    if (selected) {
        std::size_t selectedIndex = static_cast<std::size_t>(selected - editor.groundSplats().data());
        if (ImGui::BeginCombo("Splat Map", selected->name.c_str())) {
            for (std::size_t i = 0; i < editor.groundSplats().size(); ++i) {
                const auto& splat = editor.groundSplats()[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::ColorButton("##color", { splat.color.x, splat.color.y, splat.color.z, 1 });
                ImGui::SameLine();
                if (ImGui::Selectable(splat.name.c_str(), i == selectedIndex) &&
                    (!painter.dirty() || painter.save())) {
                    const bool paintingMask = painter.active();
                    editor.selectGroundSplat(i);
                    if (paintingMask && callbacks.openGroundPainting) (void)callbacks.openGroundPainting();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        selected = editor.selectedGroundSplat();
        selectedIndex = static_cast<std::size_t>(selected - editor.groundSplats().data());
        Level::GroundSplat edited = *selected;
        bool changed = ImGui::InputText("Map Name", &edited.name);
        changed |= ImGui::ColorEdit3("Assignment Color", &edited.color.x, ImGuiColorEditFlags_NoAlpha);
        const auto textureChoice = [&](const char* label, std::string& name) {
            bool picked = false;
            if (ImGui::BeginCombo(label, name.c_str())) {
                if (callbacks.assetManifest) {
                    for (const auto& texture : callbacks.assetManifest().textures()) {
                        if (ImGui::Selectable(texture.name.c_str(), texture.name == name)) {
                            name = texture.name;
                            picked = true;
                        }
                    }
                }
                ImGui::EndCombo();
            }
            return picked;
        };
        changed |= textureChoice("Base Texture", edited.base);
        changed |= textureChoice("Detail Texture", edited.detail);
        const bool maskChanged = textureChoice("Blend Mask", edited.mask);
        changed |= maskChanged;
        if (changed) {
            if (!maskChanged || !painter.dirty() || painter.save()) {
                if (maskChanged) painter.close();
                (void)editor.updateGroundSplat(selectedIndex, std::move(edited));
            }
        }
        if (selectedIndex > 0 && ImGui::Button("Remove Splat Map")) {
            if (!painter.dirty() || painter.save()) {
                painter.close();
                (void)editor.removeGroundSplat(selectedIndex);
            }
        }
        bool assigning = editor.groundAssignmentPainting();
        if (ImGui::Checkbox("Paint Tile Assignments", &assigning)) {
            if (!painter.dirty() || painter.save()) {
                painter.close();
                editor.setGroundAssignmentPainting(assigning);
            }
        }
        ImGui::Checkbox("Show Assignment Colors", &editor.showGroundAssignmentColors());
        ImGui::TextDisabled("Save the document to keep maps and tile assignments.");
        if (editor.groundAssignmentPainting()) return;
    }

    if (!painter.active()) {
        if (ImGui::Button("Paint Blend Mask") && callbacks.openGroundPainting) {
            (void)callbacks.openGroundPainting();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(edits the selected map's blend mask)");
        if (!painter.status().empty()) {
            ImGui::TextWrapped("%s", painter.status().c_str());
        }
        return;
    }

    if (ImGui::Button("Stop Painting")) {
        if (!painter.dirty() || painter.save()) painter.close();
        return;
    }
    ImGui::SameLine();
    // Saving is explicit: a mis-stroke should never reach disk on its own.
    if (ImGui::Button("Save Map")) {
        (void)painter.save();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(painter.dirty() ? "unsaved" : "saved");

    SplatCanvas::Brush& brush = painter.brush();
    // Radius is in board tiles, so the brush keeps its size on the ground
    // regardless of camera distance or board dimensions.
    drawBrushValue("Size (tiles)", brush.radiusTiles, 0.1f, 8.0f, 64.0f);
    drawBrushValue("Hardness", brush.hardness, 0.0f, 1.0f, 1.0f);
    drawBrushValue("Opacity", brush.opacity, 0.01f, 1.0f, 1.0f);

    int color = brush.color == SplatCanvas::BrushColor::White ? 0 : 1;
    ImGui::TextUnformatted("Color");
    ImGui::SameLine();
    // White adds the detail layer (rock), black returns to the base (grass).
    ImGui::RadioButton("White (detail)", &color, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Black (base)", &color, 1);
    brush.color = color == 0
        ? SplatCanvas::BrushColor::White
        : SplatCanvas::BrushColor::Black;

    if (ImGui::Button("Undo Stroke")) {
        (void)painter.undo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu stroke(s) undoable", painter.undoDepth());

    if (!painter.status().empty()) {
        ImGui::TextWrapped("%s", painter.status().c_str());
    }
#else
    (void)editor;
    (void)painter;
    (void)callbacks;
#endif
}

void LevelEditorDebugUi::syncDocumentPath(const LevelEditor& editor)
{
    filePathBuffer_ = editor.documentPath().string();
}

namespace {

std::string cellText(GridPosition3 cell)
{
    return "(" + std::to_string(cell.x) + ", " + std::to_string(cell.y) +
        ", " + std::to_string(cell.z) + ")";
}

// "2 plates -> Gate (4, 0, 1), Rotator (3, 0, 1)".
std::string linkGroupText(const LevelEditor::LinkGroup& group)
{
    std::string text = std::to_string(group.pressurePlates.size()) +
        (group.pressurePlates.size() == 1 ? " plate" : " plates");
    if (!group.objects.empty()) {
        text += ", " + std::to_string(group.objects.size()) +
            (group.objects.size() == 1 ? " linked object" : " linked objects");
    }
    if (!group.portals.empty()) {
        text +=
            ", " + std::to_string(group.portals.size()) + " portal entrances";
    }
    std::vector<std::string> devices;
    devices.reserve(group.gates.size() + group.rotators.size() +
        group.lockPlates.size() + group.elevators.size() +
        group.minecarts.size());
    for (const GridPosition3 cell : group.gates) {
        devices.push_back("Gate " + cellText(cell));
    }
    for (const GridPosition3 cell : group.rotators) {
        devices.push_back("Rotator " + cellText(cell));
    }
    for (const GridPosition3 cell : group.lockPlates) {
        devices.push_back("Lock Plate " + cellText(cell));
    }
    for (const GridPosition3 cell : group.elevators) {
        devices.push_back("Elevator " + cellText(cell));
    }
    for (const GridPosition3 cell : group.minecarts) {
        devices.push_back("Minecart " + cellText(cell));
    }
    for (std::size_t i = 0; i < devices.size(); ++i) {
        text += (i == 0 ? " -> " : ", ") + devices[i];
    }
    return text;
}

// "0, 3, 5, 7", "[0 3 5 7]" and similar: integers separated by commas,
// spaces or semicolons, optionally bracketed. Empty when anything else is
// in the text.
std::optional<std::vector<int>> parseElevatorLevels(std::string_view text)
{
    std::vector<int> levels;
    std::size_t position = 0;
    const auto separator = [](char character) {
        return character == ',' || character == ';' || character == '[' ||
            character == ']' || std::isspace(static_cast<unsigned char>(character));
    };
    while (position < text.size()) {
        if (separator(text[position])) {
            ++position;
            continue;
        }
        int value = 0;
        const char* begin = text.data() + position;
        const char* end = text.data() + text.size();
        const auto [next, error] = std::from_chars(begin, end, value);
        if (error != std::errc {} || next == begin ||
            (next != end && !separator(*next))) {
            return std::nullopt;
        }
        levels.push_back(value);
        position = static_cast<std::size_t>(next - text.data());
    }
    return levels;
}

std::string formatElevatorLevels(const std::vector<int>& levels)
{
    std::string text;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        text += (i == 0 ? "" : ", ") + std::to_string(levels[i]);
    }
    return text;
}

// One full trip from the start: 0 -> 3 -> 5 -> 7 -> 5 -> 3 -> 0 for [0, 3, 5,
// 7] starting at 0. Starting part-way along shows the trip from there.
std::string elevatorCycleText(const Level::Elevator& elevator)
{
    if (elevator.levels.size() < 2) {
        return "stays at layer " + std::to_string(elevator.cell.z);
    }
    const std::size_t stops = elevator.levels.size();
    uint8_t phase = rules::elevatorInitialPhase(elevator);
    std::string text = std::to_string(
        elevator.levels[rules::elevatorStopIndex(stops, phase)]);
    for (std::size_t step = 0; step < 2 * (stops - 1); ++step) {
        phase = rules::elevatorNextPhase(stops, phase);
        text += " -> " +
            std::to_string(
                elevator.levels[rules::elevatorStopIndex(stops, phase)]);
    }
    return text;
}

} // namespace

void LevelEditorDebugUi::drawTilePalette(
    LevelEditor& editor,
    const InputBindings& bindings,
    const Callbacks& callbacks)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    SOKOBAN_PROFILE_SCOPE("Editor.Draw tile palette");
    if (!editor.recentTiles().empty()) {
        ImGui::Text("Recent");
        const std::vector<TileType>& recent = editor.recentTiles();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float rightEdge = ImGui::GetCursorScreenPos().x +
            ImGui::GetContentRegionAvail().x;
        const auto labelFor = [&](std::size_t slot) {
            return std::to_string(slot + 1) + " " +
                std::string(tileTypeName(recent[slot]));
        };
        for (std::size_t slot = 0; slot < recent.size(); ++slot) {
            const std::string text = labelFor(slot);
            const bool selected = editor.selectedTile() == recent[slot] &&
                editor.tool() == LevelEditor::Tool::Tiles;
            if (selected) {
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            const std::string label = text + "##recent" + std::to_string(slot);
            if (ImGui::SmallButton(label.c_str())) {
                (void)editor.selectRecentTile(slot);
            }
            if (selected) {
                ImGui::PopStyleColor();
            }
            if (slot + 1 < recent.size()) {
                const float nextWidth =
                    ImGui::CalcTextSize(labelFor(slot + 1).c_str()).x +
                    style.FramePadding.x * 2.0f;
                if (ImGui::GetItemRectMax().x + style.ItemSpacing.x +
                        nextWidth < rightEdge) {
                    ImGui::SameLine();
                }
            }
        }
    }
    ImGui::Text("Paint");
    // Wrap to the panel width instead of one long row, which these buttons are
    // far too wide for.
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float available = ImGui::GetContentRegionAvail().x;
    const int perRow = std::max(
        1,
        static_cast<int>(
            (available + spacing) / (paletteButtonSize.x + spacing)));
    const bool editingOverworld = editor.editingOverworld();
    {
        SOKOBAN_PROFILE_SCOPE("Editor.Draw palette icons");
        int column = 0;
        for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
            const auto* group = editorTilePalette::groupFor(definition.type);
            if (group != nullptr && group->tiles.front() != definition.type) {
                continue;
            }
            if (definition.type == TileType::Water ||
                (!editingOverworld &&
                 definition.type == TileType::Player) ||
                (editingOverworld &&
                 (definition.type == TileType::Rogue ||
                  definition.type == TileType::Knight ||
                  definition.type == TileType::Druid ||
                  definition.type == TileType::Witch ||
                  definition.type == TileType::Bard)) ||
                (editingOverworld &&
                 definition.type == TileType::End)) {
                continue;
            }
            if (column % perRow != 0) {
                ImGui::SameLine();
            }
            // Keep the selected direction visible on its single family button.
            const TileType displayedTile = group != nullptr &&
                    group->contains(editor.selectedTile())
                ? editor.selectedTile() : definition.type;
            const auto& displayedDefinition = tileTypeDefinitions()[
                static_cast<std::size_t>(displayedTile)];
            const auto thumbnailFor = [&](TileType tile) {
                return static_cast<ImTextureID>(callbacks.tileThumbnail
                    ? callbacks.tileThumbnail(tile) : 0);
            };
            ImGui::PushID(static_cast<int>(definition.type));
            if (drawPaintButton(displayedDefinition, editor.selectedTile(),
                    thumbnailFor(displayedTile), group)) {
                if (group != nullptr) {
                    ImGui::OpenPopup("Direction");
                } else {
                    editor.setSelectedTile(definition.type);
                }
            }
            if (group != nullptr && ImGui::BeginPopup("Direction")) {
                ImGui::Text("%.*s direction",
                    static_cast<int>(group->name.size()), group->name.data());
                ImGui::Separator();
                if (ImGui::BeginTable("Variants", 2,
                        ImGuiTableFlags_SizingFixedFit)) {
                    for (TileType variant : group->variants()) {
                        ImGui::TableNextColumn();
                        const auto& variantDefinition = tileTypeDefinitions()[
                            static_cast<std::size_t>(variant)];
                        if (drawPaintButton(variantDefinition,
                                editor.selectedTile(), thumbnailFor(variant))) {
                            editor.setSelectedTile(variant);
                            ImGui::CloseCurrentPopup();
                        }
                        const auto direction = variantDefinition.name.substr(
                            group->name.size() + 1);
                        ImGui::TextUnformatted(direction.data(),
                            direction.data() + direction.size());
                    }
                    ImGui::EndTable();
                }
                if (ImGui::SmallButton("Cancel")) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
            ++column;
        }
    }

    const std::string_view selectedName = tileTypeName(editor.selectedTile());
    ImGui::Text("Selected: %.*s", static_cast<int>(selectedName.size()), selectedName.data());
    ImGui::TextWrapped(
        "Plates (Pressure, Buttons, End, Rotators, Lock Plates, Rail Stops) stack with units and mirrors; "
        "a Minecart can only stack on a Rail Stop. A Minecart Gate stacks on "
        "any rail and lifts for passing carts and their character riders; "
        "loose blocks cannot pass, even aboard a cart. Paint "
        "one onto the other in either order. Erasing lifts the unit or mirror "
        "off and leaves the plate.");

    ImGui::Separator();
    ImGui::TextUnformatted("Links");
    ImGui::TextWrapped(
        "Gates, rotators, lock plates, elevators and minecarts are driven by every pressure "
        "plate and button of their color: give sources and devices the same color to link "
        "them. A gate opens while all of its plates are pressed; a rotator "
        "turns and an elevator or minecart moves each time they all become "
        "pressed. A device with no plates of its color never activates. "
        "Movable objects of the same color repeat one another's successful "
        "moves when their own path is clear.");
    ImGui::TextWrapped(
        "Activate pulses every button occupied by a living hero and activates "
        "all eligible mirrors together. Button pulses last through the next "
        "game step; repeated presses trigger devices again without stepping off.");
    Vec3 paintColor = editor.activeLinkColor();
    if (ImGui::ColorEdit3("Link Color", &paintColor.x)) {
        editor.setActiveLinkColor(paintColor);
    }
    const std::string pickKeys =
        actionBindingsDisplay(bindings, InputAction::EditorPickTile);
    const std::string brushKeys =
        actionBindingsDisplay(bindings, InputAction::EditorPaintLinkColor);
    ImGui::BulletText("New pressure plates, devices and portals take this "
                      "color; paint it onto movable "
                      "objects to link them.");
    ImGui::BulletText(
        "%s + click: eyedropper - picks up the color of the plate, device or linked object "
        "clicked.",
        pickKeys.c_str());
    ImGui::BulletText(
        "%s + click or drag: brush - paints the color onto each plate, "
        "device or movable object it touches. Painting a linkable tile over itself does "
        "the same.",
        brushKeys.c_str());

    const auto& groups = editor.linkGroupsView();
    if (selectedLinkGroup_ &&
        std::ranges::none_of(groups, [&](const LevelEditor::LinkGroup& group) {
            return LevelEditor::sameLinkColor(group.color, *selectedLinkGroup_);
        })) {
        selectedLinkGroup_.reset();
    }
    if (groups.empty()) {
        ImGui::TextDisabled(
            "Paint matching colors onto plates and devices, or onto two "
            "movable objects, to link them.");
    }
    for (std::size_t index = 0; index < groups.size(); ++index) {
        const LevelEditor::LinkGroup& group = groups[index];
        ImGui::PushID(static_cast<int>(index));
        const bool selected = selectedLinkGroup_ &&
            LevelEditor::sameLinkColor(*selectedLinkGroup_, group.color);
        if (ImGui::ColorButton(
                "##group",
                ImVec4 { group.color.x, group.color.y, group.color.z, 1.0f },
                ImGuiColorEditFlags_NoTooltip)) {
            selectedLinkGroup_ = group.color;
            editor.setActiveLinkColor(group.color);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Select this group and paint with its color.");
        }
        ImGui::SameLine();
        const std::string text = linkGroupText(group);
        if (selected) {
            ImGui::TextColored(
                ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "%s", text.c_str());
        } else {
            ImGui::TextWrapped("%s", text.c_str());
        }
        if (group.hasDevice() && group.pressurePlates.empty()) {
            ImGui::TextColored(
                ImVec4 { 1.0f, 0.65f, 0.3f, 1.0f },
                "    No pressure plates of this color: never activates.");
        }
        if (!group.portals.empty() && group.portals.size() != 2) {
            ImGui::TextDisabled(
                "    Exactly two portals of this color form an active pair.");
        }
        if (group.objects.size() == 1) {
            ImGui::TextDisabled(
                "    Add another movable object of this color to link movement.");
        } else if (
            !group.hasDevice() && group.objects.empty() &&
            group.portals.empty()) {
            ImGui::TextDisabled("    Drives nothing yet.");
        }
        ImGui::PopID();
    }
    ImGui::BeginDisabled(!selectedLinkGroup_ ||
        LevelEditor::sameLinkColor(*selectedLinkGroup_, editor.activeLinkColor()));
    if (ImGui::Button("Recolor Selected Group to Link Color") &&
        selectedLinkGroup_) {
        const Vec3 target = editor.activeLinkColor();
        if (editor.recolorLinkGroup(*selectedLinkGroup_, target)) {
            selectedLinkGroup_ = target;
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(
            "Gives every plate, device and object of the selected group the link "
            "color. Choosing another group's color merges the two groups.");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Gates");
    ImGui::TextWrapped(
        "A closed gate is a solid block that units can stand on; an open gate "
        "is empty space. A gate opens while all its linked plates are pressed, "
        "or, when it starts open, closes while they are. Closing on a hero, "
        "enemy or turret kills it; a rock or ice block holds it open.");
    const std::vector<Level::Gate>& gates = editor.gates();
    if (selectedGateIndex_ && *selectedGateIndex_ >= gates.size()) {
        selectedGateIndex_.reset();
    }
    if (!selectedGateIndex_ && !gates.empty()) {
        selectedGateIndex_ = 0;
    }
    if (gates.empty()) {
        ImGui::TextDisabled("Paint a Gate tile to configure it here.");
    } else {
        const std::size_t index = *selectedGateIndex_;
        const std::string preview = "Gate " + cellText(gates[index].cell);
        if (ImGui::BeginCombo("Gate", preview.c_str())) {
            for (std::size_t candidate = 0; candidate < gates.size(); ++candidate) {
                const std::string label = "Gate " +
                    cellText(gates[candidate].cell) + "##gate" +
                    std::to_string(candidate);
                if (ImGui::Selectable(label.c_str(), candidate == index)) {
                    selectedGateIndex_ = candidate;
                }
            }
            ImGui::EndCombo();
        }
        bool startOpen = gates[*selectedGateIndex_].startOpen;
        if (ImGui::Checkbox("Start Open", &startOpen)) {
            (void)editor.setGateStartOpen(*selectedGateIndex_, startOpen);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Inverts the gate: open while its plates are released, closed "
                "while they are all pressed. Drawn faded in the editor.");
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Lecterns");
    ImGui::TextWrapped("Walk into a lectern to read its text. Line breaks are preserved; long text uses pages.");
    ImGui::TextWrapped("Use <!Move Up!>, <!Undo!>, or <!Activate!> to show the current binding icon. Unknown actions appear in red.");
    const auto& lecterns = editor.lecterns();
    if (!selectedLecternIndex_ || *selectedLecternIndex_ >= lecterns.size()) {
        selectedLecternIndex_ = lecterns.empty() ? std::nullopt : std::optional<std::size_t>(0);
    }
    if (lecterns.empty()) {
        ImGui::TextDisabled("Paint a Lectern tile to configure its text here.");
    } else {
        const std::string preview = "Lectern " + cellText(lecterns[*selectedLecternIndex_].cell);
        if (ImGui::BeginCombo("Lectern", preview.c_str())) {
            for (std::size_t i = 0; i < lecterns.size(); ++i) {
                const std::string label = "Lectern " + cellText(lecterns[i].cell);
                if (ImGui::Selectable(label.c_str(), i == *selectedLecternIndex_)) selectedLecternIndex_ = i;
            }
            ImGui::EndCombo();
        }
        std::string text = lecterns[*selectedLecternIndex_].text;
        const bool changed = ImGui::InputTextMultiline("Text##lectern", &text,
            ImVec2(-1.0f, ImGui::GetTextLineHeight() * 7.0f));
        if (ImGui::IsItemActivated()) (void)editor.beginStroke();
        if (changed) {
            (void)editor.setLecternText(*selectedLecternIndex_, std::move(text));
        }
        if (ImGui::IsItemDeactivated()) (void)editor.endStroke();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Lock Plates");
    ImGui::TextWrapped(
        "An enabled Lock Plate holds every unit on it in place, including heroes. "
        "All pressure plates of its color must be pressed to invert its start state.");
    const std::vector<Level::LockPlate>& lockPlates = editor.lockPlates();
    if (selectedLockPlateIndex_ && *selectedLockPlateIndex_ >= lockPlates.size()) {
        selectedLockPlateIndex_.reset();
    }
    if (!selectedLockPlateIndex_ && !lockPlates.empty()) {
        selectedLockPlateIndex_ = 0;
    }
    if (lockPlates.empty()) {
        ImGui::TextDisabled("Paint a Lock Plate tile to configure it here.");
    } else {
        const std::size_t index = *selectedLockPlateIndex_;
        const std::string preview = "Lock Plate " + cellText(lockPlates[index].cell);
        if (ImGui::BeginCombo("Lock Plate", preview.c_str())) {
            for (std::size_t candidate = 0; candidate < lockPlates.size(); ++candidate) {
                const std::string label = "Lock Plate " +
                    cellText(lockPlates[candidate].cell) + "##lockplate" +
                    std::to_string(candidate);
                if (ImGui::Selectable(label.c_str(), candidate == index)) {
                    selectedLockPlateIndex_ = candidate;
                }
            }
            ImGui::EndCombo();
        }
        bool startEnabled = lockPlates[*selectedLockPlateIndex_].startEnabled;
        if (ImGui::Checkbox("Start Enabled", &startEnabled)) {
            (void)editor.setLockPlateStartEnabled(*selectedLockPlateIndex_, startEnabled);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Enabled while plates are released; disabled while all are pressed. "
                "Without linked plates, keeps its start state.");
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Elevator Stops");
    ImGui::TextWrapped(
        "An elevator platform moves to its next stop each time it activates, "
        "carrying whatever stands on it, and turns back at the first and "
        "last stop: 0, 3, 5, 7 travels 0 -> 3 -> 5 -> 7 -> 5 -> 3 -> 0. It "
        "starts on its tile's layer, which must be one of the stops. A "
        "blocked shaft cancels the move.");
    const std::vector<Level::Elevator>& elevators = editor.elevators();
    if (selectedElevatorIndex_ && *selectedElevatorIndex_ >= elevators.size()) {
        selectedElevatorIndex_.reset();
    }
    if (!selectedElevatorIndex_ && !elevators.empty()) {
        selectedElevatorIndex_ = 0;
    }
    if (elevators.empty()) {
        ImGui::TextDisabled("Paint an Elevator tile to configure it here.");
    } else {
        const std::size_t index = *selectedElevatorIndex_;
        const std::string preview = "Elevator " + cellText(elevators[index].cell);
        if (ImGui::BeginCombo("Elevator", preview.c_str())) {
            for (std::size_t candidate = 0; candidate < elevators.size(); ++candidate) {
                const std::string label = "Elevator " +
                    cellText(elevators[candidate].cell) + "##elevator" +
                    std::to_string(candidate);
                if (ImGui::Selectable(label.c_str(), candidate == index)) {
                    selectedElevatorIndex_ = candidate;
                }
            }
            ImGui::EndCombo();
        }
        const Level::Elevator& elevator = elevators[*selectedElevatorIndex_];
        // Rebuild the text whenever the record it describes changes
        // underneath it (another elevator selected, undo, a layer inserted),
        // but never while the user is typing in it.
        const std::pair<std::size_t, std::vector<int>> source {
            *selectedElevatorIndex_, elevator.levels,
        };
        if (elevatorLevelsSource_ != source && !elevatorLevelsEditing_) {
            elevatorLevelsSource_ = source;
            elevatorLevelsBuffer_ = formatElevatorLevels(elevator.levels);
        }
        ImGui::InputText("Stop Layers", &elevatorLevelsBuffer_);
        elevatorLevelsEditing_ = ImGui::IsItemActive();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Layers the platform stops at, in travel order, e.g. "
                "0, 3, 5, 7. Press Enter or click away to apply.");
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const std::optional<std::vector<int>> parsed =
                parseElevatorLevels(elevatorLevelsBuffer_);
            elevatorLevelsError_ = !parsed;
            if (parsed) {
                (void)editor.setElevatorLevels(*selectedElevatorIndex_, *parsed);
            }
            // Show the stored stops again if the editor refuses these.
            elevatorLevelsSource_.reset();
        }
        if (elevatorLevelsError_) {
            ImGui::TextColored(
                ImVec4 { 1.0f, 0.45f, 0.35f, 1.0f },
                "Stops must be whole layer numbers, e.g. 0, 3, 5, 7.");
        }
        const std::string cycle = elevatorCycleText(elevator);
        ImGui::TextWrapped("Cycle: %s", cycle.c_str());
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Minecart Route Direction");
    ImGui::TextWrapped(
        "A minecart starts on a Rail Stop. Choose which of that stop's two "
        "exits it follows first. An open route visits the stops on that side "
        "and turns back at the end; a loop keeps circling in the chosen direction.");
    const std::vector<Level::Minecart>& minecarts = editor.minecarts();
    if (selectedMinecartIndex_ && *selectedMinecartIndex_ >= minecarts.size()) {
        selectedMinecartIndex_.reset();
    }
    if (!selectedMinecartIndex_ && !minecarts.empty()) {
        selectedMinecartIndex_ = 0;
    }
    if (minecarts.empty()) {
        ImGui::TextDisabled(
            "Paint a Minecart on a Rail Stop to configure it here.");
    } else {
        const std::size_t index = *selectedMinecartIndex_;
        const std::string preview =
            "Minecart " + cellText(minecarts[index].cell);
        if (ImGui::BeginCombo("Minecart", preview.c_str())) {
            for (std::size_t candidate = 0;
                 candidate < minecarts.size();
                 ++candidate) {
                const std::string label = "Minecart " +
                    cellText(minecarts[candidate].cell) + "##minecart" +
                    std::to_string(candidate);
                if (ImGui::Selectable(label.c_str(), candidate == index)) {
                    selectedMinecartIndex_ = candidate;
                }
            }
            ImGui::EndCombo();
        }
        const Level::Minecart& minecart = minecarts[*selectedMinecartIndex_];
        const TileType stop = editor.documentPlateAt(minecart.cell)
            .value_or(TileType::Air);
        const uint8_t mask = railConnectionMask(stop);
        constexpr std::array<const char*, 4> names {
            "North", "East", "South", "West",
        };
        for (uint8_t direction = 0; direction < 4; ++direction) {
            if ((mask & static_cast<uint8_t>(1U << direction)) == 0) {
                continue;
            }
            bool selected = minecart.initialDirection == direction;
            if (ImGui::RadioButton(names[direction], selected)) {
                (void)editor.setMinecartInitialDirection(
                    *selectedMinecartIndex_, direction);
            }
            if (direction < 3) {
                ImGui::SameLine();
            }
        }
    }

    // These pictures are screenshots of the real render, so they go stale when
    // models, materials or lighting change.
    if (ImGui::Button("Re-bake Tile Pictures") && callbacks.bakeTileThumbnails) {
        (void)callbacks.bakeTileThumbnails();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Renders every tile through the normal game path and saves the "
            "results to assets/custom/thumbnails. Takes a moment and the "
            "window will flicker through each tile.");
    }
#else
    (void)editor;
    (void)bindings;
    (void)callbacks;
#endif
}

// The mesh library half of the decoration palette: the filter, the list, and
// the deferred registration.
//
// Registration has to happen after the list iteration releases its Entry
// references, which is why the pending path is a local here rather than an
// action taken inline - the comment that says so moved with it.
void LevelEditorDebugUi::drawDecorationMeshLibrary(
    LevelEditor& editor, const Callbacks& callbacks)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    std::optional<std::filesystem::path> meshToRegister;
    if (callbacks.decorationMeshes) {
        const auto& meshes = callbacks.decorationMeshes();
        if (ImGui::BeginChild(
                "DecorationMeshFiles",
                ImVec2(0.0f, 190.0f),
                true)) {
            for (const DecorationMeshCatalog::Entry& mesh : meshes) {
                const std::string path = mesh.relativePath.generic_string();
                if (!containsInsensitive(path, decorationFilter_) &&
                    !containsInsensitive(mesh.modelName, decorationFilter_)) {
                    continue;
                }
                ImGui::PushID(path.c_str());
                const bool registered = mesh.registered();
                const bool selected = registered &&
                    editor.selectedDecorationModel() == mesh.modelName;
                const std::string label = registered
                    ? mesh.modelName
                    : path + " (add to manifest)";
                if (ImGui::Selectable(label.c_str(), selected)) {
                    if (registered) {
                        editor.setSelectedDecorationModel(mesh.modelName);
                        decorationRegistrationStatus_.clear();
                    } else if (callbacks.registerDecorationMesh) {
                        meshToRegister = mesh.relativePath;
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", path.c_str());
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }
    // Registration refreshes the catalog, so perform it only after the list
    // iteration has released all Entry references.
    if (meshToRegister && callbacks.registerDecorationMesh) {
        const std::optional<std::string> modelName =
            callbacks.registerDecorationMesh(*meshToRegister);
        if (modelName) {
            editor.setSelectedDecorationModel(*modelName);
            decorationRegistrationStatus_ =
                "Registered as " + *modelName + ".";
        } else {
            decorationRegistrationStatus_ =
                "Registration failed; see the asset log.";
        }
    }
    if (callbacks.decorationMeshStatus) {
        ImGui::TextDisabled(
            "%s", callbacks.decorationMeshStatus().c_str());
    }
    if (!decorationRegistrationStatus_.empty()) {
        ImGui::TextWrapped("%s", decorationRegistrationStatus_.c_str());
    }

#else
    (void)editor;
    (void)callbacks;
#endif
}

// The transform, point light and actions for whichever decoration is selected.
// Returns immediately when nothing is.
void LevelEditorDebugUi::drawSelectedDecorationInspector(LevelEditor& editor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    const Level::Decoration* selected = editor.selectedDecoration();
    if (!selected) {
        return;
    }

    Level::Decoration edited = *selected;
    ImGui::Text("Transform: %s", edited.model.c_str());
    bool changed = ImGui::DragFloat3(
        "Translate",
        &edited.position.x,
        0.05f,
        -1000.0f,
        1000.0f,
        "%.3f");
    changed = ImGui::DragFloat3(
                  "Rotate",
                  &edited.rotationDegrees.x,
                  1.0f,
                  -3600.0f,
                  3600.0f,
                  "%.1f deg") ||
        changed;
    changed = ImGui::DragFloat3(
                  "Scale",
                  &edited.scale.x,
                  0.02f,
                  0.001f,
                  100.0f,
                  "%.3f") ||
        changed;
    edited.scale.x = std::max(edited.scale.x, 0.001f);
    edited.scale.y = std::max(edited.scale.y, 0.001f);
    edited.scale.z = std::max(edited.scale.z, 0.001f);
    if (changed) {
        (void)editor.updateSelectedDecoration(edited);
    }

    ImGui::Separator();
    ImGui::TextDisabled(
        "Up to %zu point lights are active in one view.",
        RenderFrameData::pointLightCapacity);
    bool hasPointLight = edited.pointLight.has_value();
    if (ImGui::Checkbox("Attach Point Light", &hasPointLight)) {
        edited.pointLight = hasPointLight
            ? std::optional<Level::Decoration::PointLight> {
                  Level::Decoration::PointLight {} }
            : std::nullopt;
        (void)editor.updateSelectedDecoration(edited);
    }
    if (edited.pointLight) {
        Level::Decoration::PointLight& light = *edited.pointLight;
        bool lightChanged = ImGui::ColorEdit3("Light Color", &light.color.x);
        lightChanged = ImGui::DragFloat(
                           "Intensity", &light.intensity, 0.05f,
                           0.0f, 100.0f, "%.2f") ||
            lightChanged;
        lightChanged = ImGui::DragFloat(
                           "Range", &light.range, 0.05f,
                           0.05f, 100.0f, "%.2f") ||
            lightChanged;
        lightChanged = ImGui::DragFloat3(
                           "Local Offset", &light.offset.x, 0.02f,
                           -100.0f, 100.0f, "%.3f") ||
            lightChanged;
        lightChanged = ImGui::Checkbox(
                           "Cast Shadows", &light.castsShadows) ||
            lightChanged;
        if (light.castsShadows) {
            lightChanged = ImGui::DragFloat(
                               "Shadow Bias (World Units)",
                               &light.shadowBias, 0.001f,
                               0.0f, 0.25f, "%.3f") ||
                lightChanged;
            lightChanged = ImGui::SliderFloat(
                               "Shadow Opacity", &light.shadowOpacity,
                               0.0f, 1.0f, "%.2f") ||
                lightChanged;
        }
        light.color.x = std::max(light.color.x, 0.0f);
        light.color.y = std::max(light.color.y, 0.0f);
        light.color.z = std::max(light.color.z, 0.0f);
        light.intensity = std::max(light.intensity, 0.0f);
        light.range = std::max(light.range, 0.05f);
        light.shadowBias = std::max(light.shadowBias, 0.0f);
        light.shadowOpacity = std::clamp(light.shadowOpacity, 0.0f, 1.0f);
        if (lightChanged) {
            (void)editor.updateSelectedDecoration(edited);
        }
    }

    if (ImGui::Button("Reset Transform")) {
        edited.rotationDegrees = {};
        edited.scale = { 1.0f, 1.0f, 1.0f };
        (void)editor.updateSelectedDecoration(edited);
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")) {
        (void)editor.duplicateSelectedDecoration();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        (void)editor.deleteSelectedDecoration();
    }
#else
    (void)editor;
#endif
}

void LevelEditorDebugUi::drawDecorationPalette(
    LevelEditor& editor,
    const Callbacks& callbacks)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::TextUnformatted("Mesh Library");
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh") &&
        callbacks.refreshDecorationMeshes) {
        callbacks.refreshDecorationMeshes();
    }
    ImGui::InputTextWithHint(
        "##decoration_filter",
        "Filter mesh files",
        &decorationFilter_);

    drawDecorationMeshLibrary(editor, callbacks);
    ImGui::Separator();
    ImGui::Text("Placed Meshes (%zu)", editor.decorations().size());
    if (ImGui::BeginListBox(
            "##placed_decorations",
            ImVec2(-1.0f, 120.0f))) {
        for (std::size_t index = 0;
             index < editor.decorations().size();
             ++index) {
            const Level::Decoration& decoration =
                editor.decorations()[index];
            const std::string label =
                std::to_string(index + 1) + ": " + decoration.model;
            const bool selected =
                editor.selectedDecorationIndex() == index;
            if (ImGui::Selectable(label.c_str(), selected)) {
                (void)editor.selectDecoration(index);
            }
        }
        ImGui::EndListBox();
    }

    drawSelectedDecorationInspector(editor);
#else
    (void)editor;
    (void)callbacks;
#endif
}

void LevelEditorDebugUi::drawSelectorPalette(LevelEditor& editor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (!editor.editingOverworld()) {
        ImGui::TextWrapped(
            "Screen selectors can only be authored in overworld.scr. "
            "Open it from the Overworld tab below.");
        return;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Selector Assignments");
    ImGui::TextWrapped(
        "Click a flag on the map to select it, or select it in this list. "
        "Then choose its level and screen below.");
    ImGui::TextUnformatted("Map controls: click to place/select, D + click to delete");
    ImGui::Text("Flags (%zu)", editor.selectors().size());
    const auto& levels = editor.levelBrowserSnapshot();
    if (ImGui::BeginListBox("##screen_selectors", ImVec2(-1.0f, 130.0f))) {
        for (std::size_t index = 0; index < editor.selectors().size(); ++index) {
            const Level::ScreenSelector& selector = editor.selectors()[index];
            const std::string label =
                "Selector " + std::to_string(selector.id) + ": " +
                LevelEditor::selectorTargetLabel(selector, levels);
            const bool selected = editor.selectedSelectorIndex() == index;
            if (ImGui::Selectable(label.c_str(), selected)) {
                (void)editor.selectSelector(index);
            }
        }
        ImGui::EndListBox();
    }

    const Level::ScreenSelector* selected = editor.selectedSelector();
    if (!selected) {
        ImGui::TextDisabled(
            "Select a flag above to assign it to a puzzle screen.");
        return;
    }
    const uint32_t selectorId = selected->id;
    std::optional<LevelLocation> target = selected->target;
    const auto levelLabel = [](const LevelEditor::LevelDirectory& level) {
        return level.name.empty()
            ? "Level " + std::to_string(level.index + 1)
            : level.name;
    };
    const auto screenLabel = [](const LevelEditor::ScreenFile& screen) {
        return screen.name.empty()
            ? "Screen " + std::to_string(screen.index + 1)
            : screen.name;
    };

    ImGui::Separator();
    ImGui::Text("Assign Selector %u To", selectorId);
    ImGui::TextDisabled(
        "Current: %s",
        LevelEditor::selectorTargetLabel(*selected, levels).c_str());
    std::string levelPreview = "Unassigned";
    if (target) {
        const auto found = std::ranges::find(
            levels, target->level, &LevelEditor::LevelDirectory::index);
        if (found != levels.end()) {
            levelPreview = levelLabel(*found);
        } else {
            levelPreview = "Missing Level " +
                std::to_string(target->level + 1);
        }
    }
    if (ImGui::BeginCombo("Level", levelPreview.c_str())) {
        for (const LevelEditor::LevelDirectory& level : levels) {
            std::string label = levelLabel(level);
            const std::optional<OverworldScreenId> owner =
                editor.selectorLevelOwner(level.index);
            const bool blocked = owner &&
                owner != editor.overworldScreenId();
            if (blocked) {
                label += " (owned by overworld screen " +
                    std::to_string(*owner) + ")";
            }
            const bool current = target && target->level == level.index;
            ImGui::BeginDisabled(blocked);
            if (ImGui::Selectable(label.c_str(), current)) {
                target = LevelLocation {
                    .level = level.index,
                    .screen = level.screens.empty()
                        ? 0
                        : level.screens.front().index,
                };
                (void)editor.updateSelectedSelectorTarget(target);
            }
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }

    // Re-resolve after the level combo because updating the editor can
    // invalidate the pointer captured above.
    selected = editor.selectedSelector();
    target = selected ? selected->target : std::nullopt;
    const LevelEditor::LevelDirectory* targetLevel = nullptr;
    if (target) {
        const auto found = std::ranges::find(
            levels, target->level, &LevelEditor::LevelDirectory::index);
        if (found != levels.end()) {
            targetLevel = &*found;
        }
    }
    std::string screenPreview = "Unassigned";
    if (target && targetLevel) {
        const auto found = std::ranges::find(
            targetLevel->screens,
            target->screen,
            &LevelEditor::ScreenFile::index);
        screenPreview = found == targetLevel->screens.end()
            ? "Missing Screen " + std::to_string(target->screen + 1)
            : screenLabel(*found);
    }
    ImGui::BeginDisabled(targetLevel == nullptr);
    if (ImGui::BeginCombo("Screen", screenPreview.c_str())) {
        for (const LevelEditor::ScreenFile& screen : targetLevel->screens) {
            const std::string label = screenLabel(screen);
            const bool current = target && target->screen == screen.index;
            if (ImGui::Selectable(label.c_str(), current)) {
                (void)editor.updateSelectedSelectorTarget(LevelLocation {
                    .level = targetLevel->index,
                    .screen = screen.index,
                });
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    if (ImGui::Button("Unassign")) {
        (void)editor.updateSelectedSelectorTarget(std::nullopt);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Selector")) {
        (void)editor.deleteSelectedSelector();
    }
#else
    (void)editor;
#endif
}

void LevelEditorDebugUi::drawFileBrowser(
    LevelEditor& editor,
    OverworldMapEditor& overworldEditor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    const auto drawRootSelector = [&]() {
        ImGui::InputText("Root", &browserRootBuffer_);
        ImGui::SameLine();
        if (ImGui::Button("Set Root") &&
            editor.setBrowserRoot(browserRootBuffer_)) {
            browserRootBuffer_ = editor.browserRoot().string();
        }
    };

    if (ImGui::BeginTabBar("LevelBrowserTabs")) {
        if (ImGui::BeginTabItem("Levels")) {
            drawRootSelector();
            drawActiveLevelsTab(editor);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Overworld")) {
            drawRootSelector();
            drawOverworldTab(editor, overworldEditor);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Deleted")) {
            drawRootSelector();
            drawDeletedLevelsTab(editor);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    drawDeleteLevelConfirmation(editor);
    drawPermanentDeleteConfirmation(editor);
    drawRenamePopup(editor);
#else
    (void)editor;
    (void)overworldEditor;
#endif
}

// The overworld tab's toolbar: reload, save, undo, redo, and the modified
// marker, each disabled when its action is unavailable.
void LevelEditorDebugUi::drawOverworldToolbar(
    OverworldMapEditor& overworldEditor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::Text("Overworld Map%s", overworldEditor.dirty() ? " (modified)" : "");
    ImGui::SameLine();
    if (ImGui::Button("Reload Map")) {
        (void)overworldEditor.reload();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!overworldEditor.dirty());
    if (ImGui::Button("Save Map")) {
        (void)overworldEditor.save();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!overworldEditor.canUndo());
    if (ImGui::Button("Undo Map")) {
        (void)overworldEditor.undo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!overworldEditor.canRedo());
    if (ImGui::Button("Redo Map")) {
        (void)overworldEditor.redo();
    }
    ImGui::EndDisabled();
#else
    (void)overworldEditor;
#endif
}

// The inspector for whichever screen is selected on the map: its slot, its
// file, and the actions that act on it.
void LevelEditorDebugUi::drawSelectedOverworldScreen(
    LevelEditor& editor, OverworldMapEditor& overworldEditor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (const auto selectedId = overworldEditor.selectedScreen()) {
        const OverworldScreenSpec* selected =
            overworldEditor.screen(*selectedId);
        if (selected) {
            ImGui::SeparatorText(
                ("Screen " + std::to_string(*selectedId)).c_str());
            const std::filesystem::path path =
                overworldEditor.screenPath(*selectedId);
            auto openSelectedScreen = [&]() {
                if (editor.overworldScreenId() == *selectedId) {
                    editor.setEditingDocument(true);
                    return true;
                }
                if (!std::filesystem::is_regular_file(path)) {
                    return false;
                }
                editor.selectDocument(path);
                if (!editor.openDocument(path)) {
                    return false;
                }
                syncDocumentPath(editor);
                return true;
            };
            if (ImGui::Button("Open Screen") &&
                std::filesystem::is_regular_file(path)) {
                (void)openSelectedScreen();
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Screen")) {
                (void)overworldEditor.deleteScreen(*selectedId);
            }

            ImGui::InputInt2("Move To Slot", overworldMoveSlot_);
            if (ImGui::Button("Move Screen")) {
                (void)overworldEditor.moveScreen(
                    *selectedId,
                    { overworldMoveSlot_[0], overworldMoveSlot_[1] });
            }

            ImGui::TextWrapped(
                "To add a neighboring screen, use the +N, +E, +S, or +W "
                "button around this screen's card above. During play, regular "
                "movement rules decide whether the player can cross an edge. "
                "Place exactly one Player tile across all overworld screens.");
        }
    }

#else
    (void)editor;
    (void)overworldEditor;
#endif
}

// What the tab reports after the map itself: screens deleted this frame, and
// the editor's status line.
void LevelEditorDebugUi::drawOverworldDeletionsAndStatus(
    OverworldMapEditor& overworldEditor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    const std::vector<OverworldScreenId> deleted =
        overworldEditor.deletedScreens();
    if (!deleted.empty()) {
        ImGui::SeparatorText("Deleted Screens");
        ImGui::InputInt2("Restore To Slot", overworldRestoreSlot_);
        for (OverworldScreenId id : deleted) {
            ImGui::PushID(static_cast<int>(id));
            ImGui::Text("Screen %u", static_cast<unsigned>(id));
            ImGui::SameLine();
            if (ImGui::SmallButton("Restore")) {
                (void)overworldEditor.restoreDeletedScreen(
                    id,
                    { overworldRestoreSlot_[0], overworldRestoreSlot_[1] });
            }
            ImGui::PopID();
        }
    }

    if (!overworldEditor.status().empty()) {
        ImGui::TextWrapped("%s", overworldEditor.status().c_str());
    }
#else
    (void)overworldEditor;
#endif
}

void LevelEditorDebugUi::drawOverworldTab(
    LevelEditor& editor,
    OverworldMapEditor& overworldEditor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    const std::filesystem::path root = editor.browserRoot().lexically_normal();
    if (overworldEditorRoot_.lexically_normal() != root) {
        overworldEditorRoot_ = root;
        std::optional<std::filesystem::path> runtimeRoot;
        if (editor.sourceLevelRoot().lexically_normal() == root) {
            runtimeRoot = editor.runtimeLevelRoot();
        }
        overworldEditor.initialize(root, runtimeRoot);
    }

    drawOverworldToolbar(overworldEditor);

    if (!overworldEditor.loaded()) {
        ImGui::TextWrapped("%s", overworldEditor.status().c_str());
        const std::filesystem::path legacyPath = root / "overworld.scr";
        if (std::filesystem::is_regular_file(legacyPath)) {
            ImGui::TextDisabled(
                "This project still uses legacy overworld.scr; migrate it "
                "before using the topology editor.");
        }
        return;
    }

    const std::vector<OverworldMapEditor::ScreenSummary> screens =
        overworldEditor.screens();
    auto openScreenForEditing =
        [&](const OverworldMapEditor::ScreenSummary& screen) {
            if (editor.overworldScreenId() == screen.id) {
                editor.setEditingDocument(true);
                return true;
            }
            if (!std::filesystem::is_regular_file(screen.path)) {
                return false;
            }
            editor.selectDocument(screen.path);
            if (!editor.openDocument(screen.path)) {
                return false;
            }
            syncDocumentPath(editor);
            return true;
        };
    auto slotOccupied = [&](OverworldSlot slot) {
        return std::ranges::any_of(
            screens,
            [&](const OverworldMapEditor::ScreenSummary& candidate) {
                return candidate.slot == slot;
            });
    };
    int minX = 0;
    int maxX = 0;
    int minY = 0;
    int maxY = 0;
    for (const auto& screen : screens) {
        minX = std::min(minX, screen.slot.x);
        maxX = std::max(maxX, screen.slot.x);
        minY = std::min(minY, screen.slot.y);
        maxY = std::max(maxY, screen.slot.y);
    }

    constexpr float cardWidth = 112.0f;
    constexpr float cardHeight = 62.0f;
    constexpr float gap = 58.0f;
    constexpr float margin = 42.0f;
    const float cellWidth = cardWidth + gap;
    const float cellHeight = cardHeight + gap;
    const ImVec2 canvasSize {
        margin * 2.0f + static_cast<float>(maxX - minX + 1) * cellWidth,
        margin * 2.0f + static_cast<float>(maxY - minY + 1) * cellHeight,
    };
    ImGui::TextDisabled(
        "Select a screen, then use its +N / +E / +S / +W controls to add a neighbor.");
    if (ImGui::BeginChild(
            "OverworldMapCanvas",
            ImVec2(0.0f, 250.0f),
            true,
            ImGuiWindowFlags_HorizontalScrollbar)) {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Dummy(canvasSize);
        auto cardPosition = [&](OverworldSlot slot) {
            return ImVec2 {
                origin.x + margin + static_cast<float>(slot.x - minX) * cellWidth,
                origin.y + margin + static_cast<float>(slot.y - minY) * cellHeight,
            };
        };
        for (const auto& screen : screens) {
            ImGui::PushID(static_cast<int>(screen.id));
            const ImVec2 position = cardPosition(screen.slot);
            ImGui::SetCursorScreenPos(position);
            if (screen.selected) {
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    ImVec4(0.20f, 0.48f, 0.76f, 1.0f));
            }
            std::string label = "Screen " + std::to_string(screen.id) + "\n(" +
                std::to_string(screen.slot.x) + ", " +
                std::to_string(screen.slot.y) + ")  " +
                std::to_string(screen.selectorCount) + " flags";
            if (editor.hasInProgressDraft(screen.path)) {
                label += " *";
            }
            if (ImGui::Button(label.c_str(), ImVec2(cardWidth, cardHeight))) {
                (void)overworldEditor.selectScreen(screen.id);
                overworldMoveSlot_[0] = screen.slot.x;
                overworldMoveSlot_[1] = screen.slot.y;
            }
            const bool open = ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            if (screen.selected) {
                ImGui::PopStyleColor();
            }
            if (open) {
                (void)openScreenForEditing(screen);
            }

            if (screen.selected) {
                const struct AddControl {
                    const char* label;
                    const char* tooltip;
                    int dx;
                    int dy;
                    ImVec2 offset;
                } controls[] {
                    { "+N", "Add screen north", 0, -1,
                        { cardWidth * 0.5f - 18.0f, -28.0f } },
                    { "+E", "Add screen east", 1, 0,
                        { cardWidth + 6.0f, cardHeight * 0.5f - 10.0f } },
                    { "+S", "Add screen south", 0, 1,
                        { cardWidth * 0.5f - 18.0f, cardHeight + 6.0f } },
                    { "+W", "Add screen west", -1, 0,
                        { -42.0f, cardHeight * 0.5f - 10.0f } },
                };
                for (const AddControl& control : controls) {
                    const OverworldSlot target {
                        screen.slot.x + control.dx,
                        screen.slot.y + control.dy,
                    };
                    ImGui::SetCursorScreenPos({
                        position.x + control.offset.x,
                        position.y + control.offset.y,
                    });
                    ImGui::BeginDisabled(slotOccupied(target));
                    if (ImGui::SmallButton(control.label)) {
                        (void)overworldEditor.addAdjacentScreen(
                            screen.id, target);
                    }
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip(
                            "%s%s",
                            control.tooltip,
                            slotOccupied(target) ? " (slot occupied)" : "");
                    }
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (editor.editingOverworld()) {
        bool showNeighbors = editor.showOverworldNeighbors();
        if (ImGui::Checkbox("Show Neighboring Screens", &showNeighbors)) {
            editor.setShowOverworldNeighbors(showNeighbors);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Shows adjacent cardinal and diagonal screens as read-only "
                "context. Camera framing and editing remain limited to the "
                "current screen.");
        }
    }

    drawSelectedOverworldScreen(editor, overworldEditor);
    drawOverworldDeletionsAndStatus(overworldEditor);
#else
    (void)editor;
    (void)overworldEditor;
#endif
}

void LevelEditorDebugUi::drawActiveLevelsTab(LevelEditor& editor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    SOKOBAN_PROFILE_SCOPE("Editor.Draw level browser");
    const auto& levels = editor.levelBrowserSnapshot();
    bool browserChanged = false;

    if (ImGui::BeginChild("ActiveLevelFiles", ImVec2(0.0f, 210.0f), true)) {
        if (levels.empty() && ImGui::Button("+ Level")) {
            editor.addLevelAt(0);
            syncDocumentPath(editor);
            browserChanged = true;
        }

        for (const LevelEditor::LevelDirectory& level : levels) {
            if (browserChanged) {
                break;
            }

            ImGui::PushID(level.path.string().c_str());
            const bool selectedLevel = editor.documentPath().parent_path() == level.path;
            ImGui::SetNextItemOpen(selectedLevel, ImGuiCond_Once);
            const std::string levelLabel = level.name.empty()
                ? level.path.filename().string()
                : level.path.filename().string() + ": " + level.name;
            const bool levelOpen = ImGui::TreeNodeEx(
                levelLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::SameLine();
            if (ImGui::SmallButton("Rename")) {
                pendingRenameLevel_ = level;
                pendingRenameScreen_.reset();
                renameBuffer_ = level.name;
                renamePopupOpen_ = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("+ Before")) {
                editor.addLevelAt(level.index);
                syncDocumentPath(editor);
                browserChanged = true;
            }
            ImGui::SameLine();
            if (!browserChanged && ImGui::SmallButton("+ After")) {
                editor.addLevelAt(level.index + 1);
                syncDocumentPath(editor);
                browserChanged = true;
            }
            ImGui::SameLine();
            if (!browserChanged && ImGui::SmallButton("Delete")) {
                pendingDeleteLevel_ = level;
                deleteLevelConfirmationOpen_ = true;
                browserChanged = true;
            }

            if (levelOpen) {
                if (!browserChanged && ImGui::BeginTable("Screens", 5, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Screen");
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 58.0f);
                    ImGui::TableSetupColumn("Before", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableSetupColumn("After", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableSetupColumn("Delete", ImGuiTableColumnFlags_WidthFixed, 58.0f);

                    for (const LevelEditor::ScreenFile& screen : level.screens) {
                        if (browserChanged) {
                            break;
                        }

                        ImGui::PushID(screen.path.string().c_str());
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        std::string screenLabel = screen.name.empty()
                            ? screen.path.filename().string()
                            : screen.path.filename().string() + ": " + screen.name;
                        if (editor.hasInProgressDraft(screen.path)) {
                            screenLabel += " *";
                        }
                        if (ImGui::Selectable(
                                screenLabel.c_str(),
                                screen.path == editor.documentPath(),
                                ImGuiSelectableFlags_AllowDoubleClick)) {
                            editor.selectDocument(screen.path);
                            if (ImGui::IsMouseDoubleClicked(
                                    ImGuiMouseButton_Left)) {
                                (void)editor.openDocument(screen.path);
                            }
                            syncDocumentPath(editor);
                        }

                        ImGui::TableSetColumnIndex(1);
                        if (ImGui::SmallButton("Rename")) {
                            pendingRenameLevel_ = level;
                            pendingRenameScreen_ = screen.index;
                            renameBuffer_ = screen.name;
                            renamePopupOpen_ = true;
                        }
                        ImGui::TableSetColumnIndex(2);
                        if (ImGui::SmallButton("+ Before")) {
                            editor.addScreenAt(level, screen.index);
                            syncDocumentPath(editor);
                            browserChanged = true;
                        }
                        ImGui::TableSetColumnIndex(3);
                        if (!browserChanged && ImGui::SmallButton("+ After")) {
                            editor.addScreenAt(level, screen.index + 1);
                            syncDocumentPath(editor);
                            browserChanged = true;
                        }
                        ImGui::TableSetColumnIndex(4);
                        if (!browserChanged && ImGui::SmallButton("Delete")) {
                            editor.deleteScreen(level, screen.index);
                            syncDocumentPath(editor);
                            browserChanged = true;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
#else
    (void)editor;
#endif
}

void LevelEditorDebugUi::drawDeletedLevelsTab(LevelEditor& editor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    const auto& deletedLevels = editor.deletedLevelBrowserSnapshot();
    if (deletedLevels.empty()) {
        ImGui::TextUnformatted("No deleted levels.");
        return;
    }

    bool browserChanged = false;
    if (ImGui::BeginChild("DeletedLevelFiles", ImVec2(0.0f, 210.0f), true)) {
        for (const LevelEditor::LevelDirectory& deletedLevel : deletedLevels) {
            if (browserChanged) {
                break;
            }

            ImGui::PushID(deletedLevel.path.string().c_str());
            const bool levelOpen = ImGui::TreeNodeEx(deletedLevel.path.filename().string().c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::SameLine();
            if (ImGui::Button("Restore")) {
                editor.restoreDeletedLevel(deletedLevel.path);
                syncDocumentPath(editor);
                browserChanged = true;
            }
            ImGui::SameLine();
            if (!browserChanged && ImGui::Button("Permanently Delete")) {
                pendingPermanentDeletePath_ = deletedLevel.path;
                permanentDeleteConfirmationOpen_ = true;
                browserChanged = true;
            }

            if (levelOpen) {
                if (!browserChanged && deletedLevel.screens.empty()) {
                    ImGui::TextUnformatted("No screens.");
                }
                if (!browserChanged && ImGui::BeginTable("DeletedScreens", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Screen");
                    ImGui::TableSetupColumn("Delete", ImGuiTableColumnFlags_WidthFixed, 126.0f);
                    for (const LevelEditor::ScreenFile& screen : deletedLevel.screens) {
                        if (browserChanged) {
                            break;
                        }
                        ImGui::PushID(screen.path.string().c_str());
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(screen.path.filename().string().c_str());
                        ImGui::TableSetColumnIndex(1);
                        if (ImGui::SmallButton("Permanently Delete")) {
                            pendingPermanentDeletePath_ = screen.path;
                            permanentDeleteConfirmationOpen_ = true;
                            browserChanged = true;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
#else
    (void)editor;
#endif
}

void LevelEditorDebugUi::drawRenamePopup(LevelEditor& editor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    constexpr const char* popupName = "Name Level or Screen";
    if (renamePopupOpen_) {
        ImGui::OpenPopup(popupName);
    }

    if (ImGui::BeginPopupModal(
            popupName,
            &renamePopupOpen_,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool namingScreen = pendingRenameScreen_.has_value();
        ImGui::TextUnformatted(
            namingScreen ? "Screen name" : "Level name");
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        const bool submitted = ImGui::InputText(
            "##level_name",
            &renameBuffer_,
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::TextDisabled(
            "Leave blank to use the numbered default.");

        if (submitted || ImGui::Button("Save", ImVec2(90.0f, 0.0f))) {
            if (pendingRenameLevel_) {
                if (pendingRenameScreen_) {
                    editor.renameScreen(
                        *pendingRenameLevel_,
                        *pendingRenameScreen_,
                        renameBuffer_);
                } else {
                    editor.renameLevel(*pendingRenameLevel_, renameBuffer_);
                }
            }
            pendingRenameLevel_.reset();
            pendingRenameScreen_.reset();
            renamePopupOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
            pendingRenameLevel_.reset();
            pendingRenameScreen_.reset();
            renamePopupOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#else
    (void)editor;
#endif
}

void LevelEditorDebugUi::drawDeleteLevelConfirmation(LevelEditor& editor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    constexpr const char* popupName = "Delete Level?";
    if (deleteLevelConfirmationOpen_) {
        ImGui::OpenPopup(popupName);
    }

    if (ImGui::BeginPopupModal(popupName, &deleteLevelConfirmationOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::filesystem::path path = pendingDeleteLevel_ ? pendingDeleteLevel_->path : std::filesystem::path {};
        ImGui::Text("Delete %s?", path.filename().string().c_str());
        ImGui::TextUnformatted("The level will be moved to the Deleted tab.");
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(90.0f, 0.0f))) {
            if (pendingDeleteLevel_) {
                editor.deleteLevel(*pendingDeleteLevel_);
                syncDocumentPath(editor);
            }
            pendingDeleteLevel_.reset();
            deleteLevelConfirmationOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
            pendingDeleteLevel_.reset();
            deleteLevelConfirmationOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#else
    (void)editor;
#endif
}

void LevelEditorDebugUi::drawPermanentDeleteConfirmation(LevelEditor& editor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    constexpr const char* popupName = "Permanently Delete?";
    if (permanentDeleteConfirmationOpen_) {
        ImGui::OpenPopup(popupName);
    }

    if (ImGui::BeginPopupModal(popupName, &permanentDeleteConfirmationOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Permanently delete %s?", pendingPermanentDeletePath_.filename().string().c_str());
        ImGui::TextUnformatted("This cannot be restored from the Deleted tab.");
        ImGui::Separator();
        if (ImGui::Button("Delete Forever", ImVec2(120.0f, 0.0f))) {
            (void)editor.permanentlyDelete(pendingPermanentDeletePath_);
            pendingPermanentDeletePath_.clear();
            permanentDeleteConfirmationOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
            pendingPermanentDeletePath_.clear();
            permanentDeleteConfirmationOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#else
    (void)editor;
#endif
}

} // namespace sokoban
