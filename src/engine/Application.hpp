#pragma once

#include "engine/Input.hpp"
#include "engine/Config.hpp"
#include "engine/Level.hpp"
#include "engine/LevelEditor.hpp"
#include "engine/Math.hpp"
#include "engine/Time.hpp"
#include "engine/Window.hpp"
#include "engine/render/VulkanRenderer.hpp"

#include <array>
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
        GridPosition3 cell {};
        Vec3 renderPosition {};
        Vec3 animationStart {};
        Vec3 animationEnd {};
        float animationElapsed = 0.0f;
        float animationDuration = 0.0f;
        float animationSecondsPerTile = 0.0f;
        bool moving = false;
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
        GridPosition3 cell {};
        bool fallen = false;

        bool operator==(const MovableRecord&) const = default;
    };

    struct MoveRecord {
        GridPosition3 player {};
        bool playerDead = false;
        std::vector<MovableRecord> rocks;
    };

    struct ActionRecord {
        MoveRecord before;
        MoveRecord after;
        float animationSecondsPerTile = config::playerMoveDurationSeconds;
        bool conveyorDriven = false;
    };

    struct FallResult {
        GridPosition3 cell {};
        bool fallen = false;
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
    void updateConveyorMovement(float dt);
    void advancePlayerMovement(float dt);
    void advanceMovableAnimations(float dt);
    void startMovableAnimations(const ActionRecord& action);
    [[nodiscard]] bool completeActiveAction();
    [[nodiscard]] float activeActionDuration() const;
    [[nodiscard]] bool tryStartNextMove();
    [[nodiscard]] bool tryStartHeldMove();
    [[nodiscard]] bool tryStartMove(
        MoveDirection direction,
        bool allowPush = true,
        float animationSecondsPerTile = config::playerMoveDurationSeconds,
        bool conveyorDriven = false);
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
    [[nodiscard]] GridPosition3 movementTarget(MoveDirection direction) const;
    [[nodiscard]] GridPosition3 movementTarget(GridPosition3 origin, MoveDirection direction) const;
    [[nodiscard]] std::optional<GridPosition3> playerLadderClimbTarget(MoveDirection direction) const;
    [[nodiscard]] std::optional<GridPosition3> ladderClimbTarget(GridPosition3 ladderCell, GridPosition3 groundCell) const;
    [[nodiscard]] std::optional<MoveDirection> conveyorDirectionAt(GridPosition3 position) const;
    [[nodiscard]] Rock* rockAt(GridPosition3 position);
    [[nodiscard]] const Rock* rockAt(GridPosition3 position) const;
    [[nodiscard]] const Rock* fallenRockAt(GridPosition3 position) const;
    [[nodiscard]] std::optional<TileType> fallenTileAt(GridPosition3 position) const;
    [[nodiscard]] std::optional<TileType> fallenTileAt(const MoveRecord& record, GridPosition3 position) const;
    [[nodiscard]] const MovableRecord* movableRecordAt(const MoveRecord& record, GridPosition3 position) const;
    [[nodiscard]] const MovableRecord* fallenMovableRecordAt(const MoveRecord& record, GridPosition3 position) const;
    [[nodiscard]] bool isUnfilledWater(GridPosition3 position) const;
    [[nodiscard]] bool isUnfilledWater(GridPosition3 position, const MoveRecord& record) const;
    [[nodiscard]] bool isIceFloor(GridPosition3 position, const MoveRecord& record) const;
    [[nodiscard]] bool isPlayerWalkable(GridPosition3 position) const;
    [[nodiscard]] bool isPlayerWalkable(GridPosition3 position, const MoveRecord& record) const;
    [[nodiscard]] bool canMoveRock(GridPosition3 position, MoveDirection direction) const;
    [[nodiscard]] bool canMovableOccupy(GridPosition3 position, const MoveRecord& record, size_t movableIndex) const;
    [[nodiscard]] bool staticCellAllowsEntity(GridPosition3 position) const;
    [[nodiscard]] FallResult playerFallTarget(GridPosition3 position, const MoveRecord& record) const;
    [[nodiscard]] FallResult movableFallTarget(size_t movableIndex, GridPosition3 position, const MoveRecord& record) const;
    [[nodiscard]] GridPosition3 movableSlidingTarget(size_t movableIndex, MoveDirection direction, const MoveRecord& record) const;
    [[nodiscard]] GridPosition3 playerSlidingTarget(GridPosition3 position, MoveDirection direction, const MoveRecord& record) const;
    [[nodiscard]] bool allPressurePlatesActive() const;
    [[nodiscard]] bool isEndUnlocked() const;
    [[nodiscard]] std::filesystem::path screenPath(int levelIndex, int screenIndex) const;
    [[nodiscard]] bool screenExists(int levelIndex, int screenIndex) const;
    [[nodiscard]] RenderFrameData buildRenderFrame() const;
    [[nodiscard]] RenderFrameData buildGameplayRenderFrame() const;
    [[nodiscard]] RenderFrameData buildEditorRenderFrame() const;
    [[nodiscard]] float conveyorBeltScrollOffset() const;
    [[nodiscard]] float tileTypeToScale(TileType type) const;

    Window window_;
    VulkanRenderer renderer_;
    UiContext ui_;
    std::filesystem::path assetRoot_;
    Level level_;
    int currentLevel_ = 0;
    int currentScreen_ = 0;
    InputState input_;
    FrameTimer frameTimer_;
    GridPosition3 playerCell_ {};
    Vec3 playerRenderPosition_ {};
    uint32_t playerFacingQuarterTurns_ = 0;
    float playerAnimationTimeSeconds_ = 0.0f;
    bool playerDead_ = false;
    float sunAzimuthDegrees_ = config::sunAzimuthDegrees;
    float sunTiltDegrees_ = config::sunTiltDegrees;
    Vec3 sunColor_ { config::sunColor };
    float sunIntensity_ = config::sunIntensity;
    Vec3 ambientLightColor_ { config::ambientLightColor };
    float ambientLightIntensity_ = config::ambientLightIntensity;
    float specularStrength_ = config::specularStrength;
    float specularPower_ = config::specularPower;
    float modelShadowReceive_ = config::modelShadowReceive;
    bool shadowsEnabled_ = config::shadowsEnabled;
    float shadowOpacity_ = config::shadowOpacity;
    float shadowBias_ = config::shadowBias;
    Vec4 tileGridLineColor_ { config::tileGridLineColor };
    float tileGridLineWidth_ = config::tileGridLineWidth;
    float surfaceEntityHeight_ = config::surfaceEntityHeight;
    float surfaceEntityWidthDepth_ = config::surfaceEntityWidthDepth;
    std::array<float, tileTypeCount> tileScales_ {
        config::airTileScale,
        config::groundTileScale,
        config::wallTileScale,
        config::endTileScale,
        config::pressurePlateTileScale,
        config::playerTileScale,
        config::rockTileScale,
        config::iceTileScale,
        config::waterTileScale,
        config::ladderTileScale,
        config::conveyorTileScale,
        config::conveyorTileScale,
        config::conveyorTileScale,
        config::conveyorTileScale,
    };
    std::vector<Rock> rocks_;
    std::deque<MoveCommand> pendingCommands_;
    std::vector<ActionRecord> moveHistory_;
    std::optional<size_t> undoCursor_;
    ActionRecord activeAction_;
    LevelEditor levelEditor_;
    std::optional<GridPosition3> editorHoverCell_;
    float moveElapsed_ = 0.0f;
    float conveyorElapsed_ = 0.0f;
    float conveyorTilesPerSecond_ = config::conveyorTilesPerSecond;
    bool moving_ = false;
    bool running_ = true;
    bool quitConfirmationOpen_ = false;
    bool draftExitConfirmationOpen_ = false;
};

} // namespace sokoban
