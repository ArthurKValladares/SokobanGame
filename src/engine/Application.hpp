#pragma once

#include "engine/Input.hpp"
#include "engine/Config.hpp"
#include "engine/Level.hpp"
#include "engine/LevelEditor.hpp"
#include "engine/Math.hpp"
#include "engine/Rules.hpp"
#include "engine/Time.hpp"
#include "engine/Window.hpp"
#include "engine/render/VulkanRenderer.hpp"

#include <array>
#include <deque>
#include <filesystem>
#include <optional>
#include <vector>

namespace sokoban {

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();

private:
    // Presentation-only animation state for one movable; the authoritative
    // gameplay state lives in state_.movables at the same index.
    struct MovableVisual {
        Vec3 renderPosition {};
        Vec3 animationStart {};
        Vec3 animationEnd {};
        float animationElapsed = 0.0f;
        float animationDuration = 0.0f;
        float animationSecondsPerTile = 0.0f;
        bool moving = false;
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

    // One discrete world step (or undo/restart transition) being animated.
    // All entities animate across the same durationSeconds so consecutive
    // steps chain into visually continuous motion.
    struct ActionRecord {
        GameState before;
        GameState after;
        float durationSeconds = config::stepDurationSeconds;
        bool playerPushing = false;
        // Undo transitions animate the original step backwards: original
        // facing and animation clip are kept, and the clip plays in reverse.
        bool reversed = false;
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
    void advanceMovableAnimations(float dt);
    void startMovableAnimations(const ActionRecord& action);
    [[nodiscard]] bool completeActiveAction();
    [[nodiscard]] float activeActionDuration() const;
    [[nodiscard]] bool tryStartNextMove();
    [[nodiscard]] bool tryStartHeldMove();
    [[nodiscard]] bool tryStartWorldStep(std::optional<MoveDirection> playerInput);
    [[nodiscard]] bool tryStartUndoMove();
    [[nodiscard]] bool tryStartRestart();
    [[nodiscard]] std::optional<MoveDirection> pressedVerticalDirection() const;
    [[nodiscard]] std::optional<MoveDirection> pressedHorizontalDirection() const;
    [[nodiscard]] std::optional<MoveDirection> heldVerticalDirection() const;
    [[nodiscard]] std::optional<MoveDirection> heldHorizontalDirection() const;
    [[nodiscard]] std::optional<MoveDirection> heldPerpendicularDirection(MoveDirection direction) const;
    [[nodiscard]] bool hasPendingMove(MoveDirection direction) const;
    [[nodiscard]] bool tryStartHeldDirection(MoveDirection direction, std::optional<MoveDirection> queuedDirection);
    void applyGameState(const GameState& state);
    [[nodiscard]] ActionRecord invertActionRecord(const ActionRecord& record) const;
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
    GameState state_;
    int currentLevel_ = 0;
    int currentScreen_ = 0;
    InputState input_;
    FrameTimer frameTimer_;
    Vec3 playerRenderPosition_ {};
    uint32_t playerFacingQuarterTurns_ = 0;
    float playerAnimationTimeSeconds_ = 0.0f;
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
    std::vector<MovableVisual> movableVisuals_;
    std::deque<MoveCommand> pendingCommands_;
    std::vector<ActionRecord> moveHistory_;
    std::optional<size_t> undoCursor_;
    ActionRecord activeAction_;
    LevelEditor levelEditor_;
    std::optional<GridPosition3> editorHoverCell_;
    float moveElapsed_ = 0.0f;
    float stepDurationSeconds_ = config::stepDurationSeconds;
    rules::StepRates stepRates_ {};
    bool moving_ = false;
    // Set while rewinding: pending world motion (slides, conveyors) stays
    // frozen after an undo until the player makes a new input-driven step.
    bool autoMotionPaused_ = false;
    bool running_ = true;
    bool quitConfirmationOpen_ = false;
    bool draftExitConfirmationOpen_ = false;
};

} // namespace sokoban
