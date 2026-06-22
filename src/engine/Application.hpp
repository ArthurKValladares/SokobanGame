#pragma once

#include "engine/Input.hpp"
#include "engine/Config.hpp"
#include "engine/Level.hpp"
#include "engine/LevelEditor.hpp"
#include "engine/Math.hpp"
#include "engine/Time.hpp"
#include "engine/Window.hpp"
#include "engine/render/VulkanRenderer.hpp"

#include <deque>
#include <filesystem>
#include <optional>
#include <vector>

namespace sokoban {

enum class MoveDirection {
    Up,
    Down,
    Left,
    Right,
};

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();

private:
    struct Rock {
        TileType type = TileType::Rock;
        GridPosition cell {};
        Vec2 renderPosition {};
        bool fallen = false;
    };

    enum class MoveCommandType {
        Move,
        Undo,
        Restart,
    };

    struct MoveCommand {
        MoveCommandType type = MoveCommandType::Move;
        MoveDirection direction = MoveDirection::Up;
    };

    struct MovableRecord {
        GridPosition cell {};
        bool fallen = false;

        bool operator==(const MovableRecord&) const = default;
    };

    struct MoveRecord {
        GridPosition player {};
        bool playerDead = false;
        std::vector<MovableRecord> rocks;
    };

    struct ActionRecord {
        MoveRecord before;
        MoveRecord after;
    };

    void loadCurrentScreen();
    void applyLevel(Level level);
    void advanceScreen();
    void update(float dt);
    void drawDebugUi();
    void drawEditorModeIndicator();
    void drawQuitConfirmation();
    void drawDraftExitConfirmation();
    void updateEditorPainting();
    void queuePressedCommands();
    void advancePlayerMovement(float dt);
    [[nodiscard]] bool completeActiveAction();
    [[nodiscard]] float activeActionDuration() const;
    [[nodiscard]] bool tryStartNextMove();
    [[nodiscard]] bool tryStartHeldMove();
    [[nodiscard]] bool tryStartMove(MoveDirection direction);
    [[nodiscard]] bool tryStartUndoMove();
    [[nodiscard]] bool tryStartRestart();
    [[nodiscard]] std::optional<MoveDirection> pressedVerticalDirection() const;
    [[nodiscard]] std::optional<MoveDirection> pressedHorizontalDirection() const;
    [[nodiscard]] std::optional<MoveDirection> heldVerticalDirection() const;
    [[nodiscard]] std::optional<MoveDirection> heldHorizontalDirection() const;
    [[nodiscard]] std::optional<MoveDirection> heldPerpendicularDirection(MoveDirection direction) const;
    [[nodiscard]] bool hasPendingMove(MoveDirection direction) const;
    [[nodiscard]] bool tryStartHeldDirection(MoveDirection direction, std::optional<MoveDirection> queuedDirection);
    [[nodiscard]] MoveRecord captureMoveRecord() const;
    void applyMoveRecord(const MoveRecord& record);
    [[nodiscard]] ActionRecord invertActionRecord(const ActionRecord& record) const;
    [[nodiscard]] GridPosition movementTarget(MoveDirection direction) const;
    [[nodiscard]] GridPosition movementTarget(GridPosition origin, MoveDirection direction) const;
    [[nodiscard]] Rock* rockAt(GridPosition position);
    [[nodiscard]] const Rock* rockAt(GridPosition position) const;
    [[nodiscard]] const Rock* fallenRockAt(GridPosition position) const;
    [[nodiscard]] std::optional<TileType> fallenTileAt(GridPosition position) const;
    [[nodiscard]] std::optional<TileType> fallenTileAt(const MoveRecord& record, GridPosition position) const;
    [[nodiscard]] const MovableRecord* movableRecordAt(const MoveRecord& record, GridPosition position) const;
    [[nodiscard]] const MovableRecord* fallenMovableRecordAt(const MoveRecord& record, GridPosition position) const;
    [[nodiscard]] bool isUnfilledWater(GridPosition position) const;
    [[nodiscard]] bool isUnfilledWater(GridPosition position, const MoveRecord& record) const;
    [[nodiscard]] bool isIceFloor(GridPosition position, const MoveRecord& record) const;
    [[nodiscard]] bool isPlayerWalkable(GridPosition position) const;
    [[nodiscard]] bool isPlayerWalkable(GridPosition position, const MoveRecord& record) const;
    [[nodiscard]] bool canMoveRock(GridPosition position, MoveDirection direction) const;
    [[nodiscard]] bool canMovableOccupy(GridPosition position, const MoveRecord& record, size_t movableIndex) const;
    [[nodiscard]] GridPosition movableSlidingTarget(size_t movableIndex, MoveDirection direction, const MoveRecord& record) const;
    [[nodiscard]] GridPosition playerSlidingTarget(GridPosition position, MoveDirection direction, const MoveRecord& record) const;
    [[nodiscard]] bool allPressurePlatesActive() const;
    [[nodiscard]] bool isEndUnlocked() const;
    [[nodiscard]] std::filesystem::path screenPath(int levelIndex, int screenIndex) const;
    [[nodiscard]] bool screenExists(int levelIndex, int screenIndex) const;
    [[nodiscard]] RenderFrameData buildRenderFrame() const;
    [[nodiscard]] RenderFrameData buildGameplayRenderFrame() const;
    [[nodiscard]] RenderFrameData buildEditorRenderFrame() const;

    Window window_;
    VulkanRenderer renderer_;
    UiContext ui_;
    std::filesystem::path assetRoot_;
    Level level_;
    int currentLevel_ = 0;
    int currentScreen_ = 0;
    InputState input_;
    FrameTimer frameTimer_;
    GridPosition playerCell_ {};
    Vec2 playerRenderPosition_ {};
    bool playerDead_ = false;
    float sunAzimuthDegrees_ = config::sunAzimuthDegrees;
    float sunTiltDegrees_ = config::sunTiltDegrees;
    Vec3 sunColor_ { config::sunColor };
    float sunIntensity_ = config::sunIntensity;
    Vec3 ambientLightColor_ { config::ambientLightColor };
    float ambientLightIntensity_ = config::ambientLightIntensity;
    bool shadowsEnabled_ = config::shadowsEnabled;
    float shadowOpacity_ = config::shadowOpacity;
    float shadowBias_ = config::shadowBias;
    std::vector<Rock> rocks_;
    std::deque<MoveCommand> pendingCommands_;
    std::vector<ActionRecord> moveHistory_;
    std::optional<size_t> undoCursor_;
    ActionRecord activeAction_;
    LevelEditor levelEditor_;
    std::optional<GridPosition> editorHoverCell_;
    float moveElapsed_ = 0.0f;
    bool moving_ = false;
    bool running_ = true;
    bool quitConfirmationOpen_ = false;
    bool draftExitConfirmationOpen_ = false;
};

} // namespace sokoban
