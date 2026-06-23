#include "engine/LevelEditor.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <charconv>
#include <string_view>
#include <system_error>
#include <utility>

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#include <imgui_stdlib.h>
#endif

namespace sokoban {
namespace {

#if SOKOBAN_ENABLE_DEBUG_UI
void drawPaintButton(const TileTypeDefinition& definition, TileType& selectedTile)
{
    ImGui::SameLine();
    const bool selected = selectedTile == definition.type;
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.50f, 0.85f, 1.0f));
    }

    ImGui::PushID(static_cast<int>(definition.type));
    if (ImGui::Button("##paint_tile", ImVec2(32.0f, 28.0f))) {
        selectedTile = definition.type;
    }
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
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%.*s", static_cast<int>(definition.name.size()), definition.name.data());
    }
    ImGui::PopID();

    if (selected) {
        ImGui::PopStyleColor();
    }
}
#endif

std::filesystem::path normalizedAbsolutePath(const std::filesystem::path& path)
{
    return std::filesystem::absolute(path).lexically_normal();
}

bool pathStartsWith(const std::filesystem::path& path, const std::filesystem::path& root)
{
    const std::filesystem::path normalizedPath = normalizedAbsolutePath(path);
    const std::filesystem::path normalizedRoot = normalizedAbsolutePath(root);
    auto pathIt = normalizedPath.begin();
    auto rootIt = normalizedRoot.begin();

    for (; rootIt != normalizedRoot.end(); ++rootIt, ++pathIt) {
        if (pathIt == normalizedPath.end() || *pathIt != *rootIt) {
            return false;
        }
    }

    return true;
}

std::optional<int> parseNumberedName(std::string_view value, std::string_view prefix, std::string_view suffix = {})
{
    if (!value.starts_with(prefix) || value.size() < prefix.size() + suffix.size()) {
        return std::nullopt;
    }
    if (!suffix.empty() && !value.ends_with(suffix)) {
        return std::nullopt;
    }

    const size_t numberStart = prefix.size();
    const size_t numberEnd = value.size() - suffix.size();
    if (numberStart == numberEnd) {
        return std::nullopt;
    }

    int number = 0;
    const char* begin = value.data() + numberStart;
    const char* end = value.data() + numberEnd;
    const auto result = std::from_chars(begin, end, number);
    if (result.ec != std::errc {} || result.ptr != end || number < 0) {
        return std::nullopt;
    }

    return number;
}

std::filesystem::path levelDirectoryPath(const std::filesystem::path& root, int levelIndex)
{
    return root / ("level" + std::to_string(levelIndex));
}

std::filesystem::path screenFilePath(const std::filesystem::path& levelDirectory, int screenIndex)
{
    return levelDirectory / ("screen" + std::to_string(screenIndex) + ".scr");
}

} // namespace

void LevelEditor::initialize(
    const std::filesystem::path& sourceLevelRoot,
    const std::filesystem::path& runtimeLevelRoot,
    int currentLevel,
    int currentScreen)
{
    document_.sourceLevelRoot = sourceLevelRoot;
    document_.runtimeLevelRoot = runtimeLevelRoot;
    document_.browserRoot = sourceLevelRoot;
    document_.browserRootBuffer = document_.browserRoot.string();

    const std::filesystem::path currentSourcePath = document_.browserRoot /
        ("level" + std::to_string(currentLevel)) /
        ("screen" + std::to_string(currentScreen) + ".scr");
    if (std::filesystem::exists(currentSourcePath)) {
        loadDocument(currentSourcePath, false);
    } else {
        newDocument(document_.requestedWidth, document_.requestedHeight, false);
    }
    document_.playingDraft = false;
    document_.editingDocument = false;
    editHistory_.clear();
    editUndoCursor_.reset();
}

void LevelEditor::draw(const Callbacks& callbacks)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::Text("Document");
    ImGui::SameLine();
    ImGui::TextUnformatted(document_.dirty ? "modified" : "clean");
    ImGui::InputText("Path", &document_.filePathBuffer);

    if (ImGui::Button("Load")) {
        loadDocument(document_.filePathBuffer);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        saveDocument(document_.filePathBuffer);
    }
    ImGui::SameLine();
    if (ImGui::Button("Play Draft")) {
        playDocument(callbacks);
        ImGui::SetWindowCollapsed(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Return To Current Screen")) {
        document_.editingDocument = false;
        if (callbacks.returnToCurrentScreen) {
            callbacks.returnToCurrentScreen();
        }
        ImGui::SetWindowCollapsed(true);
    }

    ImGui::Separator();
    ImGui::Text("View: %s", document_.editingDocument ? "editing draft" : document_.playingDraft ? "playing draft" : "current screen");
    ImGui::InputInt("Width", &document_.requestedWidth);
    ImGui::InputInt("Height", &document_.requestedHeight);
    if (ImGui::Button("New")) {
        newDocument(document_.requestedWidth, document_.requestedHeight);
    }
    ImGui::SameLine();
    if (ImGui::Button("Resize")) {
        resizeDocument(document_.requestedWidth, document_.requestedHeight);
    }

    drawTilePalette();
    ImGui::Separator();
    drawFileBrowser();

    if (!document_.status.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", document_.status.c_str());
    }
#else
    (void)callbacks;
#endif
}

void LevelEditor::setPlayingDraft(bool playingDraft)
{
    document_.playingDraft = playingDraft;
    if (playingDraft) {
        document_.editingDocument = false;
    }
}

bool LevelEditor::playingDraft() const
{
    return document_.playingDraft;
}

void LevelEditor::setEditingDocument(bool editingDocument)
{
    document_.editingDocument = editingDocument;
    if (editingDocument) {
        document_.playingDraft = false;
    }
}

bool LevelEditor::editingDocument() const
{
    return document_.editingDocument;
}

void LevelEditor::markDraftSolved()
{
    document_.status = "Draft solved.";
}

void LevelEditor::paintCell(GridPosition position)
{
    if (position.y < 0 || position.x < 0 || position.y >= static_cast<int>(document_.rows.size())) {
        return;
    }

    std::string& row = document_.rows[static_cast<size_t>(position.y)];
    if (position.x >= static_cast<int>(row.size())) {
        return;
    }

    const char character = tileTypeToChar(document_.selectedTile);
    if (row[static_cast<size_t>(position.x)] == character) {
        return;
    }

    const DocumentSnapshot before = captureDocumentSnapshot();
    if (document_.selectedTile == TileType::Player) {
        for (std::string& documentRow : document_.rows) {
            std::ranges::replace(documentRow, tileTypeToChar(TileType::Player), tileTypeToChar(TileType::Empty));
        }
    }

    row[static_cast<size_t>(position.x)] = character;
    document_.dirty = true;
    recordDocumentChange(before);
}

bool LevelEditor::tryUndoEdit()
{
    if (editHistory_.empty()) {
        return false;
    }

    if (!editUndoCursor_) {
        editUndoCursor_ = editHistory_.size();
    }

    if (*editUndoCursor_ == 0) {
        return false;
    }

    --(*editUndoCursor_);
    const EditActionRecord inverse = invertEditActionRecord(editHistory_[*editUndoCursor_]);
    applyDocumentSnapshot(inverse.after);
    editHistory_.push_back(inverse);
    document_.status = "Undid editor change.";
    return true;
}

uint32_t LevelEditor::documentWidth() const
{
    return document_.rows.empty() ? 0U : static_cast<uint32_t>(document_.rows.front().size());
}

uint32_t LevelEditor::documentHeight() const
{
    return static_cast<uint32_t>(document_.rows.size());
}

const std::vector<std::string>& LevelEditor::documentRows() const
{
    return document_.rows;
}

TileType LevelEditor::selectedTile() const
{
    return document_.selectedTile;
}

void LevelEditor::drawTilePalette()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::Text("Paint");
    for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
        drawPaintButton(definition, document_.selectedTile);
    }

    const std::string_view selectedName = tileTypeName(document_.selectedTile);
    ImGui::Text("Selected: %.*s", static_cast<int>(selectedName.size()), selectedName.data());
#endif
}

void LevelEditor::drawFileBrowser()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::InputText("Root", &document_.browserRootBuffer);
    ImGui::SameLine();
    if (ImGui::Button("Set Root")) {
        std::filesystem::path root(document_.browserRootBuffer);
        if (std::filesystem::exists(root) && std::filesystem::is_directory(root)) {
            document_.browserRoot = root;
            document_.status = "Browser root changed.";
        } else {
            document_.status = "Browser root does not exist or is not a directory.";
        }
    }

    if (ImGui::BeginTabBar("LevelBrowserTabs")) {
        if (ImGui::BeginTabItem("Levels")) {
            drawActiveLevelsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Deleted")) {
            drawDeletedLevelsTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    drawDeleteLevelConfirmation();
    drawPermanentDeleteConfirmation();
#endif
}

void LevelEditor::drawActiveLevelsTab()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    const std::vector<LevelDirectory> levels = collectLevelDirectories();
    bool browserChanged = false;

    if (ImGui::BeginChild("ActiveLevelFiles", ImVec2(0.0f, 210.0f), true)) {
        if (levels.empty()) {
            if (ImGui::Button("+ Level")) {
                addLevelAt(0);
                browserChanged = true;
            }
        }

        for (const LevelDirectory& level : levels) {
            if (browserChanged) {
                break;
            }

            ImGui::PushID(level.path.string().c_str());
            const bool selectedLevel = document_.filePath.parent_path() == level.path;
            ImGui::SetNextItemOpen(selectedLevel, ImGuiCond_Once);
            const bool levelOpen = ImGui::TreeNodeEx(level.path.filename().string().c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::SameLine();
            if (ImGui::SmallButton("+ Before")) {
                addLevelAt(level.index);
                browserChanged = true;
            }
            ImGui::SameLine();
            if (!browserChanged && ImGui::SmallButton("+ After")) {
                addLevelAt(level.index + 1);
                browserChanged = true;
            }
            ImGui::SameLine();
            if (!browserChanged && ImGui::SmallButton("Delete")) {
                requestDeleteLevel(level);
                browserChanged = true;
            }

            if (levelOpen) {
                if (!browserChanged && ImGui::BeginTable("Screens", 4, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Screen");
                    ImGui::TableSetupColumn("Before", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableSetupColumn("After", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableSetupColumn("Delete", ImGuiTableColumnFlags_WidthFixed, 58.0f);

                    for (const ScreenFile& screen : level.screens) {
                        if (browserChanged) {
                            break;
                        }

                        ImGui::PushID(screen.path.string().c_str());
                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        const std::string screenLabel = screen.path.filename().string();
                        if (ImGui::Selectable(screenLabel.c_str(), screen.path == document_.filePath)) {
                            document_.filePath = screen.path;
                            document_.filePathBuffer = screen.path.string();
                            document_.status = "Selected " + screen.path.string();
                        }

                        ImGui::TableSetColumnIndex(1);
                        if (ImGui::SmallButton("+ Before")) {
                            addScreenAt(level, screen.index);
                            browserChanged = true;
                        }

                        ImGui::TableSetColumnIndex(2);
                        if (!browserChanged && ImGui::SmallButton("+ After")) {
                            addScreenAt(level, screen.index + 1);
                            browserChanged = true;
                        }

                        ImGui::TableSetColumnIndex(3);
                        if (!browserChanged && ImGui::SmallButton("Delete")) {
                            deleteScreen(level, screen.index);
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
#endif
}

void LevelEditor::drawDeletedLevelsTab()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    const std::vector<LevelDirectory> deletedLevels = collectDeletedLevels();
    if (deletedLevels.empty()) {
        ImGui::TextUnformatted("No deleted levels.");
        return;
    }

    bool browserChanged = false;
    if (ImGui::BeginChild("DeletedLevelFiles", ImVec2(0.0f, 210.0f), true)) {
        for (const LevelDirectory& deletedLevel : deletedLevels) {
            if (browserChanged) {
                break;
            }

            ImGui::PushID(deletedLevel.path.string().c_str());
            const bool levelOpen = ImGui::TreeNodeEx(deletedLevel.path.filename().string().c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::SameLine();
            if (ImGui::Button("Restore")) {
                restoreDeletedLevel(deletedLevel.path);
                browserChanged = true;
            }
            ImGui::SameLine();
            if (!browserChanged && ImGui::Button("Permanently Delete")) {
                requestPermanentDelete(deletedLevel.path);
                browserChanged = true;
            }

            if (levelOpen) {
                if (!browserChanged && deletedLevel.screens.empty()) {
                    ImGui::TextUnformatted("No screens.");
                }

                if (!browserChanged && ImGui::BeginTable("DeletedScreens", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Screen");
                    ImGui::TableSetupColumn("Delete", ImGuiTableColumnFlags_WidthFixed, 126.0f);

                    for (const ScreenFile& screen : deletedLevel.screens) {
                        if (browserChanged) {
                            break;
                        }

                        ImGui::PushID(screen.path.string().c_str());
                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(screen.path.filename().string().c_str());

                        ImGui::TableSetColumnIndex(1);
                        if (ImGui::SmallButton("Permanently Delete")) {
                            requestPermanentDelete(screen.path);
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
#endif
}

void LevelEditor::drawDeleteLevelConfirmation()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    constexpr const char* popupName = "Delete Level?";
    if (deleteLevelConfirmationOpen_) {
        ImGui::OpenPopup(popupName);
    }

    if (ImGui::BeginPopupModal(popupName, &deleteLevelConfirmationOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete %s?", pendingDeleteLevelPath_.filename().string().c_str());
        ImGui::TextUnformatted("The level will be moved to the Deleted tab.");
        ImGui::Separator();

        if (ImGui::Button("Delete", ImVec2(90.0f, 0.0f))) {
            confirmDeleteLevel();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
            deleteLevelConfirmationOpen_ = false;
            pendingDeleteLevelPath_.clear();
            pendingDeleteLevelIndex_ = -1;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
#endif
}

void LevelEditor::drawPermanentDeleteConfirmation()
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
            confirmPermanentDelete();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
            permanentDeleteConfirmationOpen_ = false;
            pendingPermanentDeletePath_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
#endif
}

void LevelEditor::newDocument(int width, int height, bool recordHistory)
{
    const DocumentSnapshot before = captureDocumentSnapshot();
    width = std::max(width, 1);
    height = std::max(height, 1);

    document_.rows.assign(static_cast<size_t>(height), std::string(static_cast<size_t>(width), tileTypeToChar(TileType::Empty)));
    document_.rows.front().front() = tileTypeToChar(TileType::Player);
    document_.requestedWidth = width;
    document_.requestedHeight = height;
    document_.dirty = true;
    document_.playingDraft = false;
    document_.editingDocument = true;
    document_.status = "Created new level.";
    if (recordHistory) {
        recordDocumentChange(before);
    }
}

void LevelEditor::resizeDocument(int width, int height, bool recordHistory)
{
    const DocumentSnapshot before = captureDocumentSnapshot();
    width = std::max(width, 1);
    height = std::max(height, 1);

    std::vector<std::string> resized(static_cast<size_t>(height), std::string(static_cast<size_t>(width), tileTypeToChar(TileType::Empty)));
    const size_t copyHeight = std::min(resized.size(), document_.rows.size());
    for (size_t y = 0; y < copyHeight; ++y) {
        const size_t copyWidth = std::min(resized[y].size(), document_.rows[y].size());
        std::copy_n(document_.rows[y].begin(), copyWidth, resized[y].begin());
    }

    document_.rows = std::move(resized);
    document_.requestedWidth = width;
    document_.requestedHeight = height;
    document_.dirty = true;
    document_.status = "Resized level.";
    if (recordHistory) {
        recordDocumentChange(before);
    }
}

void LevelEditor::loadDocument(const std::filesystem::path& path, bool recordHistory)
{
    const DocumentSnapshot before = captureDocumentSnapshot();
    std::ifstream file(path);
    if (!file) {
        document_.status = "Failed to load: " + path.string();
        return;
    }

    std::vector<std::string> rows;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        rows.push_back(line);
    }

    try {
        Level::loadFromLines(rows, path.string());
    } catch (const std::exception& error) {
        document_.status = error.what();
        return;
    }

    document_.rows = std::move(rows);
    document_.filePath = path;
    document_.filePathBuffer = path.string();
    document_.requestedHeight = static_cast<int>(document_.rows.size());
    document_.requestedWidth = document_.rows.empty() ? 1 : static_cast<int>(std::ranges::max(document_.rows, {}, &std::string::size).size());
    resizeDocument(document_.requestedWidth, document_.requestedHeight, false);
    document_.dirty = false;
    document_.playingDraft = false;
    document_.editingDocument = true;
    document_.status = "Loaded " + path.string();
    if (recordHistory) {
        recordDocumentChange(before);
    }
}

void LevelEditor::saveDocument(const std::filesystem::path& path)
{
    if (document_.rows.empty()) {
        document_.status = "Nothing to save.";
        return;
    }

    const std::filesystem::path sourcePath = normalizedAbsolutePath(path);
    std::error_code error;
    if (sourcePath.has_parent_path()) {
        std::filesystem::create_directories(sourcePath.parent_path(), error);
        if (error) {
            document_.status = "Failed to create directories: " + error.message();
            return;
        }
    }

    std::ofstream file(sourcePath, std::ios::trunc);
    if (!file) {
        document_.status = "Failed to save: " + sourcePath.string();
        return;
    }

    for (const std::string& row : document_.rows) {
        file << row << '\n';
    }
    file.close();

    const std::filesystem::path mirrorPath = runtimeMirrorPath(sourcePath);
    if (!mirrorPath.empty()) {
        if (mirrorPath.has_parent_path()) {
            std::filesystem::create_directories(mirrorPath.parent_path(), error);
            if (error) {
                document_.status = "Saved source, but failed to create runtime mirror directories: " + error.message();
                return;
            }
        }

        std::ofstream mirrorFile(mirrorPath, std::ios::trunc);
        if (!mirrorFile) {
            document_.status = "Saved source, but failed to update runtime mirror: " + mirrorPath.string();
            return;
        }

        for (const std::string& row : document_.rows) {
            mirrorFile << row << '\n';
        }
    }

    document_.filePath = sourcePath;
    document_.filePathBuffer = sourcePath.string();
    document_.dirty = false;
    document_.status = mirrorPath.empty()
        ? "Saved " + sourcePath.string()
        : "Saved " + sourcePath.string() + " and updated runtime mirror.";
}

void LevelEditor::playDocument(const Callbacks& callbacks)
{
    try {
        if (callbacks.playDraft) {
            callbacks.playDraft(documentToLevel());
        }
        document_.playingDraft = true;
        document_.editingDocument = false;
        document_.status = "Playing editor draft.";
    } catch (const std::exception& error) {
        document_.status = error.what();
    }
}

void LevelEditor::addLevelAt(int levelIndex)
{
    std::error_code error;
    std::filesystem::create_directories(document_.browserRoot, error);
    if (error) {
        document_.status = "Failed to create browser root: " + error.message();
        return;
    }

    std::vector<LevelDirectory> levels = collectLevelDirectories();
    for (auto it = levels.rbegin(); it != levels.rend(); ++it) {
        if (it->index >= levelIndex) {
            std::filesystem::rename(it->path, levelDirectoryPath(document_.browserRoot, it->index + 1), error);
            if (error) {
                document_.status = "Failed to rename level: " + error.message();
                return;
            }
        }
    }

    const std::filesystem::path newLevelPath = levelDirectoryPath(document_.browserRoot, levelIndex);
    const std::filesystem::path newScreenPath = screenFilePath(newLevelPath, 0);
    writeScreenFile(newScreenPath, defaultScreenRows());
    mirrorBrowserRootToRuntime();
    loadDocument(newScreenPath);
    document_.status = "Added " + newLevelPath.filename().string() + ".";
}

void LevelEditor::requestDeleteLevel(const LevelDirectory& level)
{
    pendingDeleteLevelPath_ = level.path;
    pendingDeleteLevelIndex_ = level.index;
    deleteLevelConfirmationOpen_ = true;
}

void LevelEditor::confirmDeleteLevel()
{
    if (pendingDeleteLevelIndex_ < 0 || pendingDeleteLevelPath_.empty()) {
        deleteLevelConfirmationOpen_ = false;
        return;
    }

    std::error_code error;
    const std::filesystem::path deletedRoot = deletedLevelRoot();
    std::filesystem::create_directories(deletedRoot, error);
    if (error) {
        document_.status = "Failed to create Deleted directory: " + error.message();
        return;
    }

    const std::filesystem::path deletedPath = uniqueDeletedLevelPath(pendingDeleteLevelPath_);
    std::filesystem::rename(pendingDeleteLevelPath_, deletedPath, error);
    if (error) {
        document_.status = "Failed to move level to Deleted: " + error.message();
        return;
    }

    std::vector<LevelDirectory> levels = collectLevelDirectories();
    for (const LevelDirectory& level : levels) {
        if (level.index > pendingDeleteLevelIndex_) {
            std::filesystem::rename(level.path, levelDirectoryPath(document_.browserRoot, level.index - 1), error);
            if (error) {
                document_.status = "Deleted level, but failed to renumber levels: " + error.message();
                break;
            }
        }
    }

    deleteLevelConfirmationOpen_ = false;
    pendingDeleteLevelPath_.clear();
    pendingDeleteLevelIndex_ = -1;
    mirrorBrowserRootToRuntime();
    loadFirstAvailableScreen();
    document_.status = "Moved level to Deleted.";
}

void LevelEditor::addScreenAt(const LevelDirectory& level, int screenIndex)
{
    std::error_code error;
    for (auto it = level.screens.rbegin(); it != level.screens.rend(); ++it) {
        if (it->index >= screenIndex) {
            std::filesystem::rename(it->path, screenFilePath(level.path, it->index + 1), error);
            if (error) {
                document_.status = "Failed to rename screen: " + error.message();
                return;
            }
        }
    }

    const std::filesystem::path newScreenPath = screenFilePath(level.path, screenIndex);
    writeScreenFile(newScreenPath, defaultScreenRows());
    mirrorBrowserRootToRuntime();
    loadDocument(newScreenPath);
    document_.status = "Added " + newScreenPath.filename().string() + ".";
}

void LevelEditor::deleteScreen(const LevelDirectory& level, int screenIndex)
{
    if (level.screens.size() <= 1) {
        document_.status = "Cannot delete the last screen in a level. Delete the level instead.";
        return;
    }

    const auto screen = std::ranges::find_if(level.screens, [screenIndex](const ScreenFile& candidate) {
        return candidate.index == screenIndex;
    });
    if (screen == level.screens.end()) {
        return;
    }

    std::error_code error;
    std::filesystem::remove(screen->path, error);
    if (error) {
        document_.status = "Failed to delete screen: " + error.message();
        return;
    }

    for (const ScreenFile& candidate : level.screens) {
        if (candidate.index > screenIndex) {
            std::filesystem::rename(candidate.path, screenFilePath(level.path, candidate.index - 1), error);
            if (error) {
                document_.status = "Deleted screen, but failed to renumber screens: " + error.message();
                return;
            }
        }
    }

    mirrorBrowserRootToRuntime();
    const int nextScreenIndex = std::min(screenIndex, static_cast<int>(level.screens.size()) - 2);
    loadDocument(screenFilePath(level.path, nextScreenIndex));
    document_.status = "Deleted screen.";
}

void LevelEditor::restoreDeletedLevel(const std::filesystem::path& deletedLevelPath)
{
    std::vector<LevelDirectory> levels = collectLevelDirectories();
    const int restoredIndex = levels.empty() ? 0 : levels.back().index + 1;
    const std::filesystem::path restoredPath = levelDirectoryPath(document_.browserRoot, restoredIndex);

    std::error_code error;
    std::filesystem::rename(deletedLevelPath, restoredPath, error);
    if (error) {
        document_.status = "Failed to restore deleted level: " + error.message();
        return;
    }

    mirrorBrowserRootToRuntime();
    const std::filesystem::path firstScreen = screenFilePath(restoredPath, 0);
    if (std::filesystem::exists(firstScreen)) {
        loadDocument(firstScreen);
    }
    document_.status = "Restored " + restoredPath.filename().string() + ".";
}

void LevelEditor::requestPermanentDelete(const std::filesystem::path& path)
{
    const std::filesystem::path normalizedPath = normalizedAbsolutePath(path);
    const std::filesystem::path normalizedDeletedRoot = normalizedAbsolutePath(deletedLevelRoot());
    if (!pathStartsWith(normalizedPath, normalizedDeletedRoot) || normalizedPath == normalizedDeletedRoot) {
        document_.status = "Permanent delete is only allowed inside the Deleted tab.";
        return;
    }

    pendingPermanentDeletePath_ = normalizedPath;
    permanentDeleteConfirmationOpen_ = true;
}

void LevelEditor::confirmPermanentDelete()
{
    if (pendingPermanentDeletePath_.empty()) {
        permanentDeleteConfirmationOpen_ = false;
        return;
    }

    std::error_code error;
    if (std::filesystem::is_directory(pendingPermanentDeletePath_, error)) {
        std::filesystem::remove_all(pendingPermanentDeletePath_, error);
    } else {
        std::filesystem::remove(pendingPermanentDeletePath_, error);
    }

    if (error) {
        document_.status = "Failed to permanently delete: " + error.message();
        return;
    }

    document_.status = "Permanently deleted " + pendingPermanentDeletePath_.filename().string() + ".";
    pendingPermanentDeletePath_.clear();
    permanentDeleteConfirmationOpen_ = false;
}

void LevelEditor::recordDocumentChange(const DocumentSnapshot& before)
{
    const DocumentSnapshot after = captureDocumentSnapshot();
    if (before.rows == after.rows &&
        before.filePath == after.filePath &&
        before.filePathBuffer == after.filePathBuffer &&
        before.requestedWidth == after.requestedWidth &&
        before.requestedHeight == after.requestedHeight &&
        before.dirty == after.dirty) {
        return;
    }

    editHistory_.push_back({
        .before = before,
        .after = after,
    });
    editUndoCursor_.reset();
}

void LevelEditor::applyDocumentSnapshot(const DocumentSnapshot& snapshot)
{
    document_.rows = snapshot.rows;
    document_.filePath = snapshot.filePath;
    document_.filePathBuffer = snapshot.filePathBuffer;
    document_.requestedWidth = snapshot.requestedWidth;
    document_.requestedHeight = snapshot.requestedHeight;
    document_.dirty = snapshot.dirty;
    document_.playingDraft = false;
    document_.editingDocument = true;
}

Level LevelEditor::documentToLevel() const
{
    return Level::loadFromLines(document_.rows, "level editor draft");
}

LevelEditor::DocumentSnapshot LevelEditor::captureDocumentSnapshot() const
{
    return {
        .rows = document_.rows,
        .filePath = document_.filePath,
        .filePathBuffer = document_.filePathBuffer,
        .requestedWidth = document_.requestedWidth,
        .requestedHeight = document_.requestedHeight,
        .dirty = document_.dirty,
    };
}

LevelEditor::EditActionRecord LevelEditor::invertEditActionRecord(const EditActionRecord& record) const
{
    return {
        .before = record.after,
        .after = record.before,
    };
}

std::filesystem::path LevelEditor::runtimeMirrorPath(const std::filesystem::path& sourcePath) const
{
    const std::filesystem::path normalizedSourceRoot = normalizedAbsolutePath(document_.sourceLevelRoot);
    const std::filesystem::path normalizedSourcePath = normalizedAbsolutePath(sourcePath);
    if (!pathStartsWith(normalizedSourcePath, normalizedSourceRoot)) {
        return {};
    }

    const std::filesystem::path relativePath = normalizedSourcePath.lexically_relative(normalizedSourceRoot);
    if (relativePath.empty()) {
        return {};
    }

    return normalizedAbsolutePath(document_.runtimeLevelRoot / relativePath);
}

std::filesystem::path LevelEditor::deletedLevelRoot() const
{
    return document_.browserRoot / "Deleted";
}

std::vector<LevelEditor::LevelDirectory> LevelEditor::collectLevelDirectories() const
{
    std::vector<LevelDirectory> levels;
    std::error_code error;
    if (!std::filesystem::exists(document_.browserRoot, error)) {
        return levels;
    }

    for (const auto& entry : std::filesystem::directory_iterator(document_.browserRoot, error)) {
        if (error) {
            break;
        }
        if (!entry.is_directory(error) || entry.path() == deletedLevelRoot()) {
            continue;
        }

        const std::optional<int> levelIndex = parseNumberedName(entry.path().filename().string(), "level");
        if (!levelIndex) {
            continue;
        }

        LevelDirectory level {
            .index = *levelIndex,
            .path = entry.path(),
        };

        for (const auto& screenEntry : std::filesystem::directory_iterator(level.path, error)) {
            if (error) {
                break;
            }
            if (!screenEntry.is_regular_file(error)) {
                continue;
            }

            const std::optional<int> screenIndex = parseNumberedName(screenEntry.path().filename().string(), "screen", ".scr");
            if (!screenIndex) {
                continue;
            }

            level.screens.push_back({
                .index = *screenIndex,
                .path = screenEntry.path(),
            });
        }

        std::ranges::sort(level.screens, {}, &ScreenFile::index);
        levels.push_back(std::move(level));
    }

    std::ranges::sort(levels, {}, &LevelDirectory::index);
    return levels;
}

std::vector<LevelEditor::LevelDirectory> LevelEditor::collectDeletedLevels() const
{
    std::vector<LevelDirectory> levels;
    std::error_code error;
    const std::filesystem::path deletedRoot = deletedLevelRoot();
    if (!std::filesystem::exists(deletedRoot, error)) {
        return levels;
    }

    for (const auto& entry : std::filesystem::directory_iterator(deletedRoot, error)) {
        if (error) {
            break;
        }
        if (entry.is_directory(error)) {
            LevelDirectory level {
                .index = parseNumberedName(entry.path().filename().string(), "level").value_or(0),
                .path = entry.path(),
            };

            for (const auto& screenEntry : std::filesystem::directory_iterator(level.path, error)) {
                if (error) {
                    break;
                }
                if (!screenEntry.is_regular_file(error)) {
                    continue;
                }

                const std::optional<int> screenIndex = parseNumberedName(screenEntry.path().filename().string(), "screen", ".scr");
                if (!screenIndex) {
                    continue;
                }

                level.screens.push_back({
                    .index = *screenIndex,
                    .path = screenEntry.path(),
                });
            }

            std::ranges::sort(level.screens, {}, &ScreenFile::index);
            levels.push_back(std::move(level));
        }
    }

    std::ranges::sort(levels, {}, &LevelDirectory::path);
    return levels;
}

std::vector<std::string> LevelEditor::defaultScreenRows() const
{
    const int width = std::max(document_.requestedWidth, 1);
    const int height = std::max(document_.requestedHeight, 1);
    std::vector<std::string> rows(static_cast<size_t>(height), std::string(static_cast<size_t>(width), tileTypeToChar(TileType::Empty)));
    rows[static_cast<size_t>(height / 2)][static_cast<size_t>(width / 2)] = tileTypeToChar(TileType::Player);
    return rows;
}

std::filesystem::path LevelEditor::uniqueDeletedLevelPath(const std::filesystem::path& levelPath) const
{
    const std::filesystem::path deletedRoot = deletedLevelRoot();
    std::filesystem::path candidate = deletedRoot / levelPath.filename();
    for (int suffix = 1; std::filesystem::exists(candidate); ++suffix) {
        candidate = deletedRoot / (levelPath.filename().string() + "_deleted" + std::to_string(suffix));
    }
    return candidate;
}

void LevelEditor::writeScreenFile(const std::filesystem::path& path, const std::vector<std::string>& rows)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        document_.status = "Failed to create screen directory: " + error.message();
        return;
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        document_.status = "Failed to write screen: " + path.string();
        return;
    }

    for (const std::string& row : rows) {
        file << row << '\n';
    }
}

void LevelEditor::mirrorBrowserRootToRuntime()
{
    const std::filesystem::path normalizedBrowserRoot = normalizedAbsolutePath(document_.browserRoot);
    const std::filesystem::path normalizedSourceRoot = normalizedAbsolutePath(document_.sourceLevelRoot);
    if (normalizedBrowserRoot != normalizedSourceRoot) {
        return;
    }

    std::error_code error;
    std::filesystem::remove_all(document_.runtimeLevelRoot, error);
    if (error) {
        document_.status = "Failed to clear runtime level mirror: " + error.message();
        return;
    }
    std::filesystem::create_directories(document_.runtimeLevelRoot, error);
    if (error) {
        document_.status = "Failed to recreate runtime level mirror: " + error.message();
        return;
    }

    for (const LevelDirectory& level : collectLevelDirectories()) {
        std::filesystem::copy(
            level.path,
            document_.runtimeLevelRoot / level.path.filename(),
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
            error);
        if (error) {
            document_.status = "Failed to update runtime level mirror: " + error.message();
            return;
        }
    }
}

void LevelEditor::loadFirstAvailableScreen()
{
    for (const LevelDirectory& level : collectLevelDirectories()) {
        if (!level.screens.empty()) {
            loadDocument(level.screens.front().path);
            return;
        }
    }

    newDocument(document_.requestedWidth, document_.requestedHeight, false);
}

} // namespace sokoban
