#include "engine/LevelEditor.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
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
const char* tileButtonLabel(TileType tile)
{
    return tile == TileType::Empty ? "." : nullptr;
}

std::string_view levelCharacterName(char character)
{
    if (const auto tile = charToTileType(character)) {
        return tileTypeName(*tile);
    }

    return "Unknown";
}

void drawPaintButton(const TileTypeDefinition& definition, TileType& selectedTile)
{
    ImGui::SameLine();
    const bool selected = selectedTile == definition.type;
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.50f, 0.85f, 1.0f));
    }

    const char* emptyLabel = tileButtonLabel(definition.type);
    std::string label = emptyLabel ? std::string(emptyLabel) : std::string(1, definition.character);
    label += "##palette_";
    label += definition.name;
    if (ImGui::Button(label.c_str(), ImVec2(32.0f, 28.0f))) {
        selectedTile = definition.type;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%.*s", static_cast<int>(definition.name.size()), definition.name.data());
    }

    if (selected) {
        ImGui::PopStyleColor();
    }
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
    document_.playingDraft = false;
    document_.editingDocument = false;
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
    if (ImGui::Button("Edit Draft")) {
        document_.playingDraft = false;
        document_.editingDocument = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Play Draft")) {
        playDocument(callbacks);
    }
    ImGui::SameLine();
    if (ImGui::Button("Return To Current Screen")) {
        document_.editingDocument = false;
        if (callbacks.returnToCurrentScreen) {
            callbacks.returnToCurrentScreen();
        }
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
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Text Grid")) {
        drawGrid();
    }

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

    if (document_.selectedTile == TileType::Player) {
        for (std::string& documentRow : document_.rows) {
            std::ranges::replace(documentRow, tileTypeToChar(TileType::Player), tileTypeToChar(TileType::Empty));
        }
    }

    row[static_cast<size_t>(position.x)] = character;
    document_.dirty = true;
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
                document_.filePath = path;
                document_.filePathBuffer = path.string();
                document_.status = "Selected " + path.string();
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
                const char* emptyLabel = tileButtonLabel(charToTileType(tile).value_or(TileType::Count));
                const std::string label = emptyLabel ? emptyLabel : std::string(1, tile);
                if (ImGui::Button(label.c_str(), ImVec2(26.0f, 24.0f))) {
                    paintCell({ static_cast<int>(x), static_cast<int>(y) });
                }
                if (ImGui::IsItemHovered()) {
                    const std::string_view name = levelCharacterName(tile);
                    ImGui::SetTooltip("(%zu, %zu) %.*s", x, y, static_cast<int>(name.size()), name.data());
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

    document_.rows.assign(static_cast<size_t>(height), std::string(static_cast<size_t>(width), tileTypeToChar(TileType::Empty)));
    document_.rows.front().front() = tileTypeToChar(TileType::Player);
    document_.requestedWidth = width;
    document_.requestedHeight = height;
    document_.dirty = true;
    document_.playingDraft = false;
    document_.editingDocument = true;
    document_.status = "Created new level.";
}

void LevelEditor::resizeDocument(int width, int height)
{
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
    document_.playingDraft = false;
    document_.editingDocument = true;
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
        document_.editingDocument = false;
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
