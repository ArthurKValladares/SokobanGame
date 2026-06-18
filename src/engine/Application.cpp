#include "engine/Application.hpp"

#include "engine/Config.hpp"
#include "engine/DebugUi.hpp"

#include <SDL3/SDL.h>

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#include <imgui_stdlib.h>
#endif

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <system_error>
#include <utility>

namespace sokoban {
namespace {

Vec2 toVec2(GridPosition position)
{
    return { static_cast<float>(position.x), static_cast<float>(position.y) };
}

Vec2 lerp(Vec2 from, Vec2 to, float t)
{
    return {
        from.x + (to.x - from.x) * t,
        from.y + (to.y - from.y) * t,
    };
}

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

Application::Application()
    : window_("Sokoban 3D", 1280, 720)
    , renderer_(window_.nativeHandle(), SOKOBAN_ASSET_DIR)
    , assetRoot_(SOKOBAN_ASSET_DIR)
{
    loadCurrentScreen();

#if SOKOBAN_ENABLE_DEBUG_UI
    initializeEditor();
    DebugUi::addWindow("Engine", [this] {
        drawDebugUi();
    });
    DebugUi::addWindow("Level Editor", [this] {
        drawLevelEditorUi();
    });
#endif
}

Application::~Application()
{
    DebugUi::clearWindows();
    renderer_.waitIdle();
}

void Application::run()
{
    while (running_) {
        input_.beginFrame();

        SDL_Event event {};
        while (SDL_PollEvent(&event)) {
            renderer_.handleEvent(event);

            if (!renderer_.wantsKeyboardCapture() || event.type == SDL_EVENT_KEY_UP) {
                input_.handleEvent(event);
            }

            if (event.type == SDL_EVENT_QUIT) {
                running_ = false;
            }

            if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) {
                running_ = false;
            }
        }

        const float dt = frameTimer_.tick();
        update(dt);

        renderer_.beginDebugUiFrame();
        DebugUi::draw();
        renderer_.drawFrame(buildRenderFrame());
    }
}

void Application::update(float dt)
{
    queuePressedCommands();
    advancePlayerMovement(dt);
}

void Application::drawDebugUi()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::Text("Level %d Screen %d", currentLevel_, currentScreen_);
    ImGui::Text("Player (%d, %d)", playerCell_.x, playerCell_.y);
    ImGui::Text("Rocks %zu", rocks_.size());
    ImGui::Text("History %zu", moveHistory_.size());
    ImGui::Text("End %s", isEndUnlocked() ? "unlocked" : "locked");
#endif
}

void Application::loadCurrentScreen()
{
    applyLevel(Level::loadFromFile(screenPath(currentLevel_, currentScreen_)));
    editor_.playingDraft = false;

    std::cerr << "player started level " << currentLevel_ << " screen " << currentScreen_ << '\n';
}

void Application::applyLevel(Level level)
{
    level_ = std::move(level);
    playerCell_ = level_.playerStart();
    playerRenderPosition_ = toVec2(playerCell_);
    rocks_.clear();
    rocks_.reserve(level_.rocks().size());
    for (GridPosition rockPosition : level_.rocks()) {
        rocks_.push_back({
            .cell = rockPosition,
            .renderPosition = toVec2(rockPosition),
        });
    }
    pendingCommands_.clear();
    moveHistory_.clear();
    undoCursor_.reset();
    activeAction_ = {};
    moving_ = false;
    moveElapsed_ = 0.0f;
}

void Application::advanceScreen()
{
    if (screenExists(currentLevel_, currentScreen_ + 1)) {
        ++currentScreen_;
    } else if (screenExists(currentLevel_ + 1, 0)) {
        ++currentLevel_;
        currentScreen_ = 0;
    } else {
        currentLevel_ = 0;
        currentScreen_ = 0;
    }

    loadCurrentScreen();
}

void Application::queuePressedCommands()
{
    if (input_.keyPressed(SDL_SCANCODE_Z)) {
        pendingCommands_.push_back({ .type = MoveCommandType::Undo });
    }
    if (input_.keyPressed(SDL_SCANCODE_R)) {
        pendingCommands_.push_back({ .type = MoveCommandType::Restart });
    }

    const std::optional<MoveDirection> vertical = pressedVerticalDirection();
    const std::optional<MoveDirection> horizontal = pressedHorizontalDirection();
    if (vertical) {
        pendingCommands_.push_back({ .type = MoveCommandType::Move, .direction = *vertical });
    }
    if (horizontal) {
        pendingCommands_.push_back({ .type = MoveCommandType::Move, .direction = *horizontal });
    }
}

void Application::advancePlayerMovement(float dt)
{
    float remainingTime = dt;

    while (remainingTime > 0.0f) {
        if (!moving_ && !tryStartNextMove()) {
            return;
        }

        if constexpr (config::playerMoveDurationSeconds <= 0.0f) {
            if (completeActiveAction()) {
                return;
            }
            continue;
        }

        constexpr float duration = config::playerMoveDurationSeconds;
        const float timeToFinish = duration - moveElapsed_;
        const float step = std::min(remainingTime, timeToFinish);
        remainingTime -= step;
        moveElapsed_ += step;

        const float t = std::clamp(moveElapsed_ / duration, 0.0f, 1.0f);
        playerRenderPosition_ = lerp(toVec2(activeAction_.before.player), toVec2(activeAction_.after.player), t);
        for (size_t i = 0; i < rocks_.size(); ++i) {
            rocks_[i].renderPosition = lerp(toVec2(activeAction_.before.rocks[i]), toVec2(activeAction_.after.rocks[i]), t);
        }

        if (moveElapsed_ >= duration) {
            if (completeActiveAction()) {
                return;
            }
        }
    }
}

bool Application::completeActiveAction()
{
    applyMoveRecord(activeAction_.after);

    moveHistory_.push_back(activeAction_);
    moving_ = false;
    moveElapsed_ = 0.0f;

    if (level_.isEnd(playerCell_) && isEndUnlocked()) {
        if (editor_.playingDraft) {
            editor_.status = "Draft solved.";
            return false;
        }

        advanceScreen();
        return true;
    }

    return false;
}

bool Application::tryStartNextMove()
{
    while (!pendingCommands_.empty()) {
        const MoveCommand command = pendingCommands_.front();
        pendingCommands_.pop_front();

        if (command.type == MoveCommandType::Undo && tryStartUndoMove()) {
            return true;
        }

        if (command.type == MoveCommandType::Restart && tryStartRestart()) {
            return true;
        }

        if (command.type == MoveCommandType::Move && tryStartHeldDirection(command.direction, heldPerpendicularDirection(command.direction))) {
            return true;
        }
    }

    return tryStartHeldMove();
}

bool Application::tryStartHeldMove()
{
    if (input_.keyDown(SDL_SCANCODE_Z)) {
        return tryStartUndoMove();
    }

    const std::optional<MoveDirection> vertical = heldVerticalDirection();
    const std::optional<MoveDirection> horizontal = heldHorizontalDirection();
    if (vertical && tryStartHeldDirection(*vertical, horizontal)) {
        return true;
    }
    if (horizontal && tryStartHeldDirection(*horizontal, vertical)) {
        return true;
    }

    return false;
}

bool Application::tryStartMove(MoveDirection direction)
{
    const GridPosition target = movementTarget(direction);
    if (!level_.isWalkable(target)) {
        return false;
    }

    activeAction_ = {
        .before = captureMoveRecord(),
        .after = captureMoveRecord(),
    };
    activeAction_.after.player = target;

    if (Rock* rock = rockAt(target)) {
        if (!canMoveRock(target, direction)) {
            return false;
        }

        const size_t rockIndex = static_cast<size_t>(rock - rocks_.data());
        activeAction_.after.rocks[rockIndex] = movementTarget(rock->cell, direction);
    }

    undoCursor_.reset();
    moveElapsed_ = 0.0f;
    moving_ = true;
    return true;
}

bool Application::tryStartUndoMove()
{
    if (moveHistory_.empty()) {
        return false;
    }

    if (!undoCursor_) {
        // A contiguous undo run walks backward through the pre-existing history while
        // each completed undo still appends its inverse move for future branching.
        undoCursor_ = moveHistory_.size();
    }

    if (*undoCursor_ == 0) {
        return false;
    }

    --(*undoCursor_);
    activeAction_ = invertActionRecord(moveHistory_[*undoCursor_]);
    moveElapsed_ = 0.0f;
    moving_ = true;
    return true;
}

bool Application::tryStartRestart()
{
    MoveRecord restarted {
        .player = level_.playerStart(),
    };
    restarted.rocks.reserve(level_.rocks().size());
    for (GridPosition rockPosition : level_.rocks()) {
        restarted.rocks.push_back(rockPosition);
    }

    const MoveRecord current = captureMoveRecord();
    if (current.player == restarted.player && current.rocks == restarted.rocks) {
        return false;
    }

    activeAction_ = {
        .before = current,
        .after = std::move(restarted),
    };
    undoCursor_.reset();
    moveElapsed_ = 0.0f;
    moving_ = true;
    return true;
}

void Application::initializeEditor()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    editor_.browserRoot = std::filesystem::current_path() / "levels";
    if (!std::filesystem::exists(editor_.browserRoot)) {
        editor_.browserRoot = assetRoot_ / "levels";
    }
    editor_.browserRootBuffer = editor_.browserRoot.string();

    const std::filesystem::path currentSourcePath = editor_.browserRoot /
        ("level" + std::to_string(currentLevel_)) /
        ("screen" + std::to_string(currentScreen_) + ".scr");
    if (std::filesystem::exists(currentSourcePath)) {
        loadEditorDocument(currentSourcePath);
    } else {
        newEditorDocument(editor_.requestedWidth, editor_.requestedHeight);
    }
#endif
}

void Application::drawLevelEditorUi()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::Text("Document");
    ImGui::SameLine();
    ImGui::TextUnformatted(editor_.dirty ? "modified" : "clean");
    ImGui::InputText("Path", &editor_.filePathBuffer);

    if (ImGui::Button("Load")) {
        loadEditorDocument(editor_.filePathBuffer);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        saveEditorDocument(editor_.filePathBuffer);
    }
    ImGui::SameLine();
    if (ImGui::Button("Play Draft")) {
        playEditorDocument();
    }
    ImGui::SameLine();
    if (ImGui::Button("Return To Current Screen")) {
        loadCurrentScreen();
    }

    ImGui::Separator();
    ImGui::InputInt("Width", &editor_.requestedWidth);
    ImGui::InputInt("Height", &editor_.requestedHeight);
    if (ImGui::Button("New")) {
        newEditorDocument(editor_.requestedWidth, editor_.requestedHeight);
    }
    ImGui::SameLine();
    if (ImGui::Button("Resize")) {
        resizeEditorDocument(editor_.requestedWidth, editor_.requestedHeight);
    }

    drawEditorTilePalette();
    ImGui::Separator();
    drawEditorFileBrowser();
    ImGui::Separator();
    drawEditorGrid();

    if (!editor_.status.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", editor_.status.c_str());
    }
#endif
}

void Application::drawEditorTilePalette()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    constexpr char tiles[] { '#', ' ', 'E', 'P', 'R', 'C' };
    ImGui::Text("Paint");
    for (char tile : tiles) {
        ImGui::SameLine();
        const bool selected = editor_.selectedTile == tile;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.50f, 0.85f, 1.0f));
        }

        const char* emptyLabel = tileButtonLabel(tile);
        std::string label = emptyLabel ? std::string(emptyLabel) : std::string(1, tile);
        label += "##palette_";
        label += tileName(tile);
        if (ImGui::Button(label.c_str(), ImVec2(32.0f, 28.0f))) {
            editor_.selectedTile = tile;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tileName(tile));
        }

        if (selected) {
            ImGui::PopStyleColor();
        }
    }
    ImGui::Text("Selected: %s", tileName(editor_.selectedTile));
#endif
}

void Application::drawEditorFileBrowser()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::InputText("Root", &editor_.browserRootBuffer);
    ImGui::SameLine();
    if (ImGui::Button("Set Root")) {
        std::filesystem::path root(editor_.browserRootBuffer);
        if (std::filesystem::exists(root) && std::filesystem::is_directory(root)) {
            editor_.browserRoot = root;
            editor_.status = "Browser root changed.";
        } else {
            editor_.status = "Browser root does not exist or is not a directory.";
        }
    }

    std::vector<std::filesystem::path> screens;
    std::error_code error;
    if (std::filesystem::exists(editor_.browserRoot, error)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(editor_.browserRoot, error)) {
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
            const std::string relative = std::filesystem::relative(path, editor_.browserRoot, error).string();
            const std::string label = error ? path.string() : relative;
            if (ImGui::Selectable(label.c_str(), path == editor_.filePath)) {
                loadEditorDocument(path);
            }
        }
    }
    ImGui::EndChild();
#endif
}

void Application::drawEditorGrid()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (editor_.rows.empty()) {
        ImGui::TextUnformatted("No document loaded.");
        return;
    }

    ImGui::Text("Grid %zu x %zu", editor_.rows.front().size(), editor_.rows.size());
    if (ImGui::BeginChild("LevelGrid", ImVec2(0.0f, 360.0f), true, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (size_t y = 0; y < editor_.rows.size(); ++y) {
            for (size_t x = 0; x < editor_.rows[y].size(); ++x) {
                ImGui::PushID(static_cast<int>(y * editor_.rows[y].size() + x));
                char tile = editor_.rows[y][x];
                const char* emptyLabel = tileButtonLabel(tile);
                const std::string label = emptyLabel ? emptyLabel : std::string(1, tile);
                if (ImGui::Button(label.c_str(), ImVec2(26.0f, 24.0f))) {
                    if (editor_.selectedTile == 'C') {
                        for (std::string& row : editor_.rows) {
                            std::ranges::replace(row, 'C', ' ');
                        }
                    }
                    editor_.rows[y][x] = editor_.selectedTile;
                    editor_.dirty = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("(%zu, %zu) %s", x, y, tileName(tile));
                }
                ImGui::PopID();
                if (x + 1 < editor_.rows[y].size()) {
                    ImGui::SameLine(0.0f, 1.0f);
                }
            }
        }
    }
    ImGui::EndChild();
#endif
}

void Application::newEditorDocument(int width, int height)
{
    width = std::max(width, 1);
    height = std::max(height, 1);

    editor_.rows.assign(static_cast<size_t>(height), std::string(static_cast<size_t>(width), ' '));
    editor_.rows.front().front() = 'C';
    editor_.requestedWidth = width;
    editor_.requestedHeight = height;
    editor_.dirty = true;
    editor_.status = "Created new level.";
}

void Application::resizeEditorDocument(int width, int height)
{
    width = std::max(width, 1);
    height = std::max(height, 1);

    std::vector<std::string> resized(static_cast<size_t>(height), std::string(static_cast<size_t>(width), ' '));
    const size_t copyHeight = std::min(resized.size(), editor_.rows.size());
    for (size_t y = 0; y < copyHeight; ++y) {
        const size_t copyWidth = std::min(resized[y].size(), editor_.rows[y].size());
        std::copy_n(editor_.rows[y].begin(), copyWidth, resized[y].begin());
    }

    editor_.rows = std::move(resized);
    editor_.requestedWidth = width;
    editor_.requestedHeight = height;
    editor_.dirty = true;
    editor_.status = "Resized level.";
}

void Application::loadEditorDocument(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) {
        editor_.status = "Failed to load: " + path.string();
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
        editor_.status = error.what();
        return;
    }

    editor_.rows = std::move(rows);
    editor_.filePath = path;
    editor_.filePathBuffer = path.string();
    editor_.requestedHeight = static_cast<int>(editor_.rows.size());
    editor_.requestedWidth = editor_.rows.empty() ? 1 : static_cast<int>(std::ranges::max(editor_.rows, {}, &std::string::size).size());
    resizeEditorDocument(editor_.requestedWidth, editor_.requestedHeight);
    editor_.dirty = false;
    editor_.status = "Loaded " + path.string();
}

void Application::saveEditorDocument(const std::filesystem::path& path)
{
    if (editor_.rows.empty()) {
        editor_.status = "Nothing to save.";
        return;
    }

    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            editor_.status = "Failed to create directories: " + error.message();
            return;
        }
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        editor_.status = "Failed to save: " + path.string();
        return;
    }

    for (const std::string& row : editor_.rows) {
        file << row << '\n';
    }

    editor_.filePath = path;
    editor_.filePathBuffer = path.string();
    editor_.dirty = false;
    editor_.status = "Saved " + path.string();
}

void Application::playEditorDocument()
{
    try {
        applyLevel(editorDocumentToLevel());
        editor_.playingDraft = true;
        editor_.status = "Playing editor draft.";
    } catch (const std::exception& error) {
        editor_.status = error.what();
    }
}

Level Application::editorDocumentToLevel() const
{
    return Level::loadFromLines(editor_.rows, "level editor draft");
}

std::optional<MoveDirection> Application::pressedVerticalDirection() const
{
    const bool upPressed = input_.keyPressed(SDL_SCANCODE_W);
    const bool downPressed = input_.keyPressed(SDL_SCANCODE_S);
    if (upPressed == downPressed) {
        return std::nullopt;
    }

    if ((upPressed && input_.keyDown(SDL_SCANCODE_S)) || (downPressed && input_.keyDown(SDL_SCANCODE_W))) {
        return std::nullopt;
    }

    return upPressed ? MoveDirection::Up : MoveDirection::Down;
}

std::optional<MoveDirection> Application::pressedHorizontalDirection() const
{
    const bool leftPressed = input_.keyPressed(SDL_SCANCODE_A);
    const bool rightPressed = input_.keyPressed(SDL_SCANCODE_D);
    if (leftPressed == rightPressed) {
        return std::nullopt;
    }

    if ((leftPressed && input_.keyDown(SDL_SCANCODE_D)) || (rightPressed && input_.keyDown(SDL_SCANCODE_A))) {
        return std::nullopt;
    }

    return leftPressed ? MoveDirection::Left : MoveDirection::Right;
}

std::optional<MoveDirection> Application::heldVerticalDirection() const
{
    const bool up = input_.keyDown(SDL_SCANCODE_W);
    const bool down = input_.keyDown(SDL_SCANCODE_S);
    if (up == down) {
        return std::nullopt;
    }

    return up ? MoveDirection::Up : MoveDirection::Down;
}

std::optional<MoveDirection> Application::heldHorizontalDirection() const
{
    const bool left = input_.keyDown(SDL_SCANCODE_A);
    const bool right = input_.keyDown(SDL_SCANCODE_D);
    if (left == right) {
        return std::nullopt;
    }

    return left ? MoveDirection::Left : MoveDirection::Right;
}

std::optional<MoveDirection> Application::heldPerpendicularDirection(MoveDirection direction) const
{
    switch (direction) {
    case MoveDirection::Up:
    case MoveDirection::Down:
        return heldHorizontalDirection();
    case MoveDirection::Left:
    case MoveDirection::Right:
        return heldVerticalDirection();
    }

    return std::nullopt;
}

bool Application::hasPendingMove(MoveDirection direction) const
{
    return std::ranges::any_of(pendingCommands_, [direction](const MoveCommand& command) {
        return command.type == MoveCommandType::Move && command.direction == direction;
    });
}

bool Application::tryStartHeldDirection(MoveDirection direction, std::optional<MoveDirection> queuedDirection)
{
    if (!tryStartMove(direction)) {
        return false;
    }

    if (queuedDirection && !hasPendingMove(*queuedDirection)) {
        pendingCommands_.push_back({ .type = MoveCommandType::Move, .direction = *queuedDirection });
    }

    return true;
}

Application::MoveRecord Application::captureMoveRecord() const
{
    MoveRecord record {
        .player = playerCell_,
    };
    record.rocks.reserve(rocks_.size());
    for (const Rock& rock : rocks_) {
        record.rocks.push_back(rock.cell);
    }

    return record;
}

void Application::applyMoveRecord(const MoveRecord& record)
{
    playerCell_ = record.player;
    playerRenderPosition_ = toVec2(playerCell_);

    for (size_t i = 0; i < rocks_.size(); ++i) {
        rocks_[i].cell = record.rocks[i];
        rocks_[i].renderPosition = toVec2(rocks_[i].cell);
    }
}

Application::ActionRecord Application::invertActionRecord(const ActionRecord& record) const
{
    return {
        .before = record.after,
        .after = record.before,
    };
}

GridPosition Application::movementTarget(MoveDirection direction) const
{
    return movementTarget(playerCell_, direction);
}

GridPosition Application::movementTarget(GridPosition origin, MoveDirection direction) const
{
    GridPosition target = origin;

    switch (direction) {
    case MoveDirection::Up:
        target.y -= 1;
        break;
    case MoveDirection::Down:
        target.y += 1;
        break;
    case MoveDirection::Left:
        target.x -= 1;
        break;
    case MoveDirection::Right:
        target.x += 1;
        break;
    }

    return target;
}

Application::Rock* Application::rockAt(GridPosition position)
{
    const auto rock = std::ranges::find_if(rocks_, [position](const Rock& candidate) {
        return candidate.cell == position;
    });

    return rock != rocks_.end() ? &*rock : nullptr;
}

const Application::Rock* Application::rockAt(GridPosition position) const
{
    const auto rock = std::ranges::find_if(rocks_, [position](const Rock& candidate) {
        return candidate.cell == position;
    });

    return rock != rocks_.end() ? &*rock : nullptr;
}

bool Application::canMoveRock(GridPosition position, MoveDirection direction) const
{
    const GridPosition target = movementTarget(position, direction);
    return level_.isWalkable(target) && rockAt(target) == nullptr;
}

bool Application::allPressurePlatesActive() const
{
    return std::ranges::all_of(level_.pressurePlates(), [this](GridPosition plate) {
        return playerCell_ == plate || rockAt(plate) != nullptr;
    });
}

bool Application::isEndUnlocked() const
{
    return allPressurePlatesActive();
}

std::filesystem::path Application::screenPath(int levelIndex, int screenIndex) const
{
    return assetRoot_ /
        "levels" /
        ("level" + std::to_string(levelIndex)) /
        ("screen" + std::to_string(screenIndex) + ".scr");
}

bool Application::screenExists(int levelIndex, int screenIndex) const
{
    if (levelIndex < 0 || screenIndex < 0) {
        return false;
    }

    return std::filesystem::exists(screenPath(levelIndex, screenIndex));
}

RenderFrameData Application::buildRenderFrame() const
{
    RenderFrameData frame;
    frame.levelWidth = level_.width();
    frame.levelHeight = level_.height();
    frame.playerPosition = playerRenderPosition_;
    const bool endUnlocked = isEndUnlocked();

    frame.tiles.reserve(static_cast<size_t>(level_.width()) * level_.height());
    for (uint32_t y = 0; y < level_.height(); ++y) {
        for (uint32_t x = 0; x < level_.width(); ++x) {
            const TileType tile = level_.tileAt(x, y);
            Vec4 color;

            switch (tile) {
            case TileType::Wall:
                color = { 0.62f, 0.32f, 0.09f, 1.0f };
                break;
            case TileType::End:
                color = endUnlocked ? Vec4 { 1.0f, 0.05f, 0.04f, 1.0f } : Vec4 { 0.38f, 0.04f, 0.04f, 1.0f };
                break;
            case TileType::PressurePlate:
                color = { 0.18f, 0.18f, 0.18f, 1.0f };
                break;
            case TileType::Empty:
                color = { 0.82f, 0.82f, 0.84f, 1.0f };
                break;
            }

            frame.tiles.push_back({
                .position = { static_cast<float>(x), static_cast<float>(y) },
                .color = color,
            });
        }
    }

    frame.tiles.push_back({
        .position = playerRenderPosition_,
        .color = { 0.0f, 1.0f, 0.15f, 1.0f },
    });

    for (const Rock& rock : rocks_) {
        frame.tiles.push_back({
            .position = rock.renderPosition,
            .color = { 0.20f, 0.10f, 0.04f, 1.0f },
        });
    }

    return frame;
}

} // namespace sokoban
