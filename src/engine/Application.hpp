#pragma once

#include "engine/Input.hpp"
#include "engine/Config.hpp"
#include "engine/GameplaySession.hpp"
#include "engine/Level.hpp"
#include "engine/LevelEditor.hpp"
#include "engine/Math.hpp"
#include "engine/Rules.hpp"
#include "engine/Time.hpp"
#include "engine/Window.hpp"
#include "engine/render/VulkanRenderer.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
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
    // Presentation-only motion state for one entity; the authoritative
    // gameplay state lives in gameplaySession_. Each entity animates
    // independently.
    struct EntityVisual {
        Vec3 renderPosition {};
        Vec3 animationStart {};
        Vec3 animationEnd {};
        float animationElapsed = 0.0f;
        float animationDuration = 0.0f;
        float animationSecondsPerTile = 0.0f;
        bool moving = false;
    };

    // The player's visual state: motion like any entity, plus the skeletal
    // clip to play while moving, a per-entity clip clock, playback direction
    // (negative while rewinding), and facing.
    struct PlayerVisual {
        EntityVisual motion;
        RenderAnimation movingClip = RenderAnimation::RogueMovement;
        float clipTimeSeconds = 0.0f;
        float clipPlaybackRate = 1.0f;
        uint32_t facingQuarterTurns = 0;
    };

    // Debug-only animation browser: previews any clip from any glTF/GLB in
    // the source assets tree on the player model, overriding the gameplay
    // animation while active.
    struct AnimationPreviewState {
        std::vector<std::filesystem::path> files;
        std::vector<std::string> fileLabels;
        int fileIndex = -1;
        std::vector<std::string> clipNames;
        int clipIndex = -1;
        std::optional<GltfAnimationClip> clip;
        std::string error;
        float time = 0.0f;
        float speed = 1.0f;
        bool playing = true;
        bool active = false;
        bool scanned = false;
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
    void updateAnimationPreview(float dt);
    void drawAnimationPreviewUi();
    void rescanAnimationPreviewFiles();
    void advancePlayerMovement(float dt);
    void advanceEntityAnimations(float dt);
    void startActionAnimations(const GameplaySession::Action& action);
    [[nodiscard]] bool completeActiveAction();
    [[nodiscard]] bool tryStartNextMove();
    [[nodiscard]] std::optional<MoveDirection> pressedVerticalDirection() const;
    [[nodiscard]] std::optional<MoveDirection> pressedHorizontalDirection() const;
    [[nodiscard]] std::optional<MoveDirection> heldVerticalDirection() const;
    [[nodiscard]] std::optional<MoveDirection> heldHorizontalDirection() const;
    void syncVisualsToGameState();
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
    GameplaySession gameplaySession_;
    int currentLevel_ = 0;
    int currentScreen_ = 0;
    InputState input_;
    FrameTimer frameTimer_;
    PlayerVisual playerVisual_;
    // Presentation clock for level geometry (conveyor belt scrolling, editor
    // previews); runs backwards while an undo transition is animating.
    float worldAnimationTimeSeconds_ = 0.0f;
    float sunAzimuthDegrees_ = config::sunAzimuthDegrees;
    float sunTiltDegrees_ = config::sunTiltDegrees;
    Vec3 sunColor_ { config::sunColor };
    float sunIntensity_ = config::sunIntensity;
    Vec3 ambientLightColor_ { config::ambientLightColor };
    float ambientLightIntensity_ = config::ambientLightIntensity;
    float specularStrength_ = config::specularStrength;
    float specularPower_ = config::specularPower;
    float modelShadowReceive_ = config::modelShadowReceive;
    bool ambientOcclusionEnabled_ = config::ambientOcclusionEnabled;
    float ambientOcclusionStrength_ = config::ambientOcclusionStrength;
    bool ambientOcclusionVisualize_ = false;
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
    std::vector<EntityVisual> movableVisuals_;
    LevelEditor levelEditor_;
    AnimationPreviewState animationPreview_;
    std::optional<GridPosition3> editorHoverCell_;
    bool running_ = true;
    bool quitConfirmationOpen_ = false;
    bool draftExitConfirmationOpen_ = false;
};

} // namespace sokoban
