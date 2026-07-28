#include "engine/LevelEditorDebugUi.hpp"

#include "engine/TileTypes.hpp"

#include <algorithm>
#include <exception>
#include <string_view>
#include <utility>

#ifndef SOKOBAN_ENABLE_DEBUG_UI
#define SOKOBAN_ENABLE_DEBUG_UI 0
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
    ImTextureID thumbnail)
{
    const bool selected = selectedTile == definition.type;
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
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "%.*s",
                static_cast<int>(definition.name.size()),
                definition.name.data());
        }
        ImGui::PopID();
        if (selected) {
            ImGui::PopStyleColor();
        }
        return clicked;
    }

    // No thumbnail: either thumbnails are unavailable, the asset is still
    // loading, or this tile is a procedural cube with no model - for which a
    // flat colour swatch is an honest depiction anyway.
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
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%.*s", static_cast<int>(definition.name.size()), definition.name.data());
    }
    ImGui::PopID();

    if (selected) {
        ImGui::PopStyleColor();
    }
    return clicked;
}
#endif

} // namespace

void LevelEditorDebugUi::initialize(const LevelEditor& editor)
{
    syncDocumentPath(editor);
    browserRootBuffer_ = editor.browserRoot().string();
    requestedWidth_ = editor.requestedWidth();
    requestedHeight_ = editor.requestedHeight();
}

void LevelEditorDebugUi::draw(
    LevelEditor& editor,
    SplatPainter& painter,
    const Callbacks& callbacks)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::Text("Document");
    ImGui::SameLine();
    ImGui::TextUnformatted(editor.dirty() ? "modified" : "clean");
    ImGui::InputText("Path", &filePathBuffer_);

    if (ImGui::Button("Load")) {
        if (editor.loadDocument(filePathBuffer_)) {
            syncDocumentPath(editor);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        if (editor.saveDocument(filePathBuffer_)) {
            syncDocumentPath(editor);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Play Draft")) {
        if (std::optional<Level> level = editor.beginDraftPlayback(); level && callbacks.playDraft) {
            callbacks.playDraft(std::move(*level));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Return To Current Screen")) {
        editor.setEditingDocument(false);
        if (callbacks.returnToCurrentScreen) {
            callbacks.returnToCurrentScreen();
        }
    }

    ImGui::Separator();
    ImGui::Text("View: %s", editor.editingDocument() ? "editing draft" : editor.playingDraft() ? "playing draft" : "current screen");
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
    if (!editor.layerLocked()) {
        ImGui::TextDisabled("Click: add above   R + click: replace   D + click: delete");
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

    drawTilePalette(editor, callbacks);
    ImGui::Separator();
    drawGroundPaintTab(painter, callbacks);
    ImGui::Separator();
    drawFileBrowser(editor);

    if (!editor.status().empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", editor.status().c_str());
    }
#else
    (void)editor;
    (void)painter;
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
    SplatPainter& painter, const Callbacks& callbacks)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::Text("Ground Paint");

    if (!painter.active()) {
        if (ImGui::Button("Paint Ground") && callbacks.openGroundPainting) {
            (void)callbacks.openGroundPainting();
        }
        ImGui::SameLine();
        // A screen added in the editor has no map and no manifest entry yet.
        // This does both, so that never means leaving the game to re-run the
        // generator; it will not overwrite an existing map.
        if (ImGui::Button("Create Splat Map") &&
            callbacks.createGroundSplatMap) {
            (void)callbacks.createGroundSplatMap();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(edits this screen's splat map)");
        if (!painter.status().empty()) {
            ImGui::TextWrapped("%s", painter.status().c_str());
        }
        return;
    }

    if (ImGui::Button("Stop Painting")) {
        painter.close();
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
    ImGui::RadioButton("White (rock)", &color, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Black (grass)", &color, 1);
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
    (void)painter;
    (void)callbacks;
#endif
}

void LevelEditorDebugUi::syncDocumentPath(const LevelEditor& editor)
{
    filePathBuffer_ = editor.documentPath().string();
}

void LevelEditorDebugUi::drawTilePalette(
    LevelEditor& editor, const Callbacks& callbacks)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::Text("Paint");
    // Wrap to the panel width instead of one long row, which these buttons are
    // far too wide for.
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float available = ImGui::GetContentRegionAvail().x;
    const int perRow = std::max(
        1,
        static_cast<int>(
            (available + spacing) / (paletteButtonSize.x + spacing)));
    int column = 0;
    for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
        if (definition.type == TileType::Water) {
            continue;
        }
        if (column % perRow != 0) {
            ImGui::SameLine();
        }
        const auto thumbnail = static_cast<ImTextureID>(
            callbacks.tileThumbnail
                ? callbacks.tileThumbnail(definition.type)
                : 0);
        if (drawPaintButton(definition, editor.selectedTile(), thumbnail)) {
            editor.setSelectedTile(definition.type);
        }
        ++column;
    }

    const std::string_view selectedName = tileTypeName(editor.selectedTile());
    ImGui::Text("Selected: %.*s", static_cast<int>(selectedName.size()), selectedName.data());

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
#endif
}

void LevelEditorDebugUi::drawFileBrowser(LevelEditor& editor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::InputText("Root", &browserRootBuffer_);
    ImGui::SameLine();
    if (ImGui::Button("Set Root") && editor.setBrowserRoot(browserRootBuffer_)) {
        browserRootBuffer_ = editor.browserRoot().string();
    }

    if (ImGui::BeginTabBar("LevelBrowserTabs")) {
        if (ImGui::BeginTabItem("Levels")) {
            drawActiveLevelsTab(editor);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Deleted")) {
            drawDeletedLevelsTab(editor);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    drawDeleteLevelConfirmation(editor);
    drawPermanentDeleteConfirmation(editor);
#else
    (void)editor;
#endif
}

void LevelEditorDebugUi::drawActiveLevelsTab(LevelEditor& editor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    const std::vector<LevelEditor::LevelDirectory> levels = editor.collectLevelDirectories();
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
            const bool levelOpen = ImGui::TreeNodeEx(level.path.filename().string().c_str(), ImGuiTreeNodeFlags_DefaultOpen);
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
                if (!browserChanged && ImGui::BeginTable("Screens", 4, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Screen");
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
                        const std::string screenLabel = screen.path.filename().string();
                        if (ImGui::Selectable(screenLabel.c_str(), screen.path == editor.documentPath())) {
                            editor.selectDocument(screen.path);
                            syncDocumentPath(editor);
                        }

                        ImGui::TableSetColumnIndex(1);
                        if (ImGui::SmallButton("+ Before")) {
                            editor.addScreenAt(level, screen.index);
                            syncDocumentPath(editor);
                            browserChanged = true;
                        }
                        ImGui::TableSetColumnIndex(2);
                        if (!browserChanged && ImGui::SmallButton("+ After")) {
                            editor.addScreenAt(level, screen.index + 1);
                            syncDocumentPath(editor);
                            browserChanged = true;
                        }
                        ImGui::TableSetColumnIndex(3);
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
    const std::vector<LevelEditor::LevelDirectory> deletedLevels = editor.collectDeletedLevels();
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
