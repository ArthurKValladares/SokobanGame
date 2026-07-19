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
bool drawPaintButton(const TileTypeDefinition& definition, TileType selectedTile)
{
    ImGui::SameLine();
    const bool selected = selectedTile == definition.type;
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.50f, 0.85f, 1.0f));
    }

    ImGui::PushID(static_cast<int>(definition.type));
    const bool clicked = ImGui::Button("##paint_tile", ImVec2(32.0f, 28.0f));
    const ImVec2 buttonMin = ImGui::GetItemRectMin();
    const ImVec2 buttonMax = ImGui::GetItemRectMax();
    const Vec4 color = tileColor(definition.type);
    const ImVec2 swatchMin { buttonMin.x + 8.0f, buttonMin.y + 6.0f };
    const ImVec2 swatchMax { buttonMax.x - 8.0f, buttonMax.y - 6.0f };
    ImDrawList* drawList = ImGui::GetWindowDrawList();
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

void LevelEditorDebugUi::draw(LevelEditor& editor, const Callbacks& callbacks)
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
    bool layerLocked = editor.layerLocked();
    if (ImGui::Checkbox("Lock Edits To Current Layer", &layerLocked)) {
        editor.setLayerLocked(layerLocked);
    }
    if (!editor.layerLocked()) {
        ImGui::TextDisabled("Click: add above   R + click: replace   D + click: delete");
    }
    if (ImGui::Button("+ Layer Above")) {
        editor.addLayer();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Layer")) {
        editor.deleteActiveLayer();
    }

    drawTilePalette(editor);
    ImGui::Separator();
    drawFileBrowser(editor);

    if (!editor.status().empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", editor.status().c_str());
    }
#else
    (void)editor;
    (void)callbacks;
#endif
}

void LevelEditorDebugUi::syncDocumentPath(const LevelEditor& editor)
{
    filePathBuffer_ = editor.documentPath().string();
}

void LevelEditorDebugUi::drawTilePalette(LevelEditor& editor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::Text("Paint");
    for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
        if (drawPaintButton(definition, editor.selectedTile())) {
            editor.setSelectedTile(definition.type);
        }
    }

    const std::string_view selectedName = tileTypeName(editor.selectedTile());
    ImGui::Text("Selected: %.*s", static_cast<int>(selectedName.size()), selectedName.data());
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
