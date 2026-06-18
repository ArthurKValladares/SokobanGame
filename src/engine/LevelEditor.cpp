#include "engine/LevelEditor.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <system_error>
#include <utility>

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#include <imgui_stdlib.h>
#endif

namespace sokoban {
namespace {

#if SOKOBAN_ENABLE_DEBUG_UI
const char* tileName(char tile)
{
    switch (tile) {
    case ' ':
        return "Empty";
    case '#':
        return "Wall";
    case 'E':
        return "End";
    case 'P':
        return "Pressure";
    case 'R':
        return "Rock";
    case 'C':
        return "Player";
    default:
        return "Unknown";
    }
}

const char* tileButtonLabel(char tile)
{
    return tile == ' ' ? "." : nullptr;
}
#endif

} // namespace

void LevelEditor::initialize(const std::filesystem::path& assetRoot, int currentLevel, int currentScreen)
{
    document_.browserRoot = std::filesystem::current_path() / "levels";
    if (!std::filesystem::exists(document_.browserRoot)) {
        document_.browserRoot = assetRoot / "levels";
    }
    document_.browserRootBuffer = document_.browserRoot.string();

    const std::filesystem::path currentSourcePath = document_.browserRoot /
        ("level" + std::to_string(currentLevel)) /
        ("screen" + std::to_string(currentScreen) + ".scr");
    if (std::filesystem::exists(currentSourcePath)) {
        loadDocument(currentSourcePath);
    } else {
        newDocument(document_.requestedWidth, document_.requestedHeight);
    }
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
    }
    ImGui::SameLine();
    if (ImGui::Button("Return To Current Screen")) {
        if (callbacks.returnToCurrentScreen) {
            callbacks.returnToCurrentScreen();
        }
    }

    ImGui::Separator();
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
    ImGui::Separator();
    drawGrid();

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
}

bool LevelEditor::playingDraft() const
{
    return document_.playingDraft;
}

void LevelEditor::markDraftSolved()
{
    document_.status = "Draft solved.";
}

void LevelEditor::drawTilePalette()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    constexpr char tiles[] { '#', ' ', 'E', 'P', 'R', 'C' };
    ImGui::Text("Paint");
    for (char tile : tiles) {
        ImGui::SameLine();
        const bool selected = document_.selectedTile == tile;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.50f, 0.85f, 1.0f));
        }

        const char* emptyLabel = tileButtonLabel(tile);
        std::string label = emptyLabel ? std::string(emptyLabel) : std::string(1, tile);
        label += "##palette_";
        label += tileName(tile);
        if (ImGui::Button(label.c_str(), ImVec2(32.0f, 28.0f))) {
            document_.selectedTile = tile;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tileName(tile));
        }

        if (selected) {
            ImGui::PopStyleColor();
        }
    }
    ImGui::Text("Selected: %s", tileName(document_.selectedTile));
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

    std::vector<std::filesystem::path> screens;
    std::error_code error;
    if (std::filesystem::exists(document_.browserRoot, error)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(document_.browserRoot, error)) {
            if (error) {
                break;
            }
            if (entry.is_regular_file(error) && entry.path().extension() == ".scr") {
                screens.push_back(entry.path());
            }
        }
    }
    std::ranges::sort(screens);

    if (ImGui::BeginChild("LevelFiles", ImVec2(0.0f, 150.0f), true)) {
        for (const auto& path : screens) {
            const std::string relative = std::filesystem::relative(path, document_.browserRoot, error).string();
            const std::string label = error ? path.string() : relative;
            if (ImGui::Selectable(label.c_str(), path == document_.filePath)) {
                loadDocument(path);
            }
        }
    }
    ImGui::EndChild();
#endif
}

void LevelEditor::drawGrid()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (document_.rows.empty()) {
        ImGui::TextUnformatted("No document loaded.");
        return;
    }

    ImGui::Text("Grid %zu x %zu", document_.rows.front().size(), document_.rows.size());
    if (ImGui::BeginChild("LevelGrid", ImVec2(0.0f, 360.0f), true, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (size_t y = 0; y < document_.rows.size(); ++y) {
            for (size_t x = 0; x < document_.rows[y].size(); ++x) {
                ImGui::PushID(static_cast<int>(y * document_.rows[y].size() + x));
                char tile = document_.rows[y][x];
                const char* emptyLabel = tileButtonLabel(tile);
                const std::string label = emptyLabel ? emptyLabel : std::string(1, tile);
                if (ImGui::Button(label.c_str(), ImVec2(26.0f, 24.0f))) {
                    if (document_.selectedTile == 'C') {
                        for (std::string& row : document_.rows) {
                            std::ranges::replace(row, 'C', ' ');
                        }
                    }
                    document_.rows[y][x] = document_.selectedTile;
                    document_.dirty = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("(%zu, %zu) %s", x, y, tileName(tile));
                }
                ImGui::PopID();
                if (x + 1 < document_.rows[y].size()) {
                    ImGui::SameLine(0.0f, 1.0f);
                }
            }
        }
    }
    ImGui::EndChild();
#endif
}

void LevelEditor::newDocument(int width, int height)
{
    width = std::max(width, 1);
    height = std::max(height, 1);

    document_.rows.assign(static_cast<size_t>(height), std::string(static_cast<size_t>(width), ' '));
    document_.rows.front().front() = 'C';
    document_.requestedWidth = width;
    document_.requestedHeight = height;
    document_.dirty = true;
    document_.status = "Created new level.";
}

void LevelEditor::resizeDocument(int width, int height)
{
    width = std::max(width, 1);
    height = std::max(height, 1);

    std::vector<std::string> resized(static_cast<size_t>(height), std::string(static_cast<size_t>(width), ' '));
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
}

void LevelEditor::loadDocument(const std::filesystem::path& path)
{
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
    resizeDocument(document_.requestedWidth, document_.requestedHeight);
    document_.dirty = false;
    document_.status = "Loaded " + path.string();
}

void LevelEditor::saveDocument(const std::filesystem::path& path)
{
    if (document_.rows.empty()) {
        document_.status = "Nothing to save.";
        return;
    }

    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            document_.status = "Failed to create directories: " + error.message();
            return;
        }
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        document_.status = "Failed to save: " + path.string();
        return;
    }

    for (const std::string& row : document_.rows) {
        file << row << '\n';
    }

    document_.filePath = path;
    document_.filePathBuffer = path.string();
    document_.dirty = false;
    document_.status = "Saved " + path.string();
}

void LevelEditor::playDocument(const Callbacks& callbacks)
{
    try {
        if (callbacks.playDraft) {
            callbacks.playDraft(documentToLevel());
        }
        document_.playingDraft = true;
        document_.status = "Playing editor draft.";
    } catch (const std::exception& error) {
        document_.status = error.what();
    }
}

Level LevelEditor::documentToLevel() const
{
    return Level::loadFromLines(document_.rows, "level editor draft");
}

} // namespace sokoban
