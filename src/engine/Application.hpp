#pragma once

#include "engine/Input.hpp"
#include "engine/Level.hpp"
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
        GridPosition cell {};
        Vec2 renderPosition {};
    };

    enum class MoveCommandType {
        Move,
        Undo,
    };

    struct MoveCommand {
        MoveCommandType type = MoveCommandType::Move;
        MoveDirection direction = MoveDirection::Up;
    };

    struct MoveRecord {
        GridPosition playerStart {};
        GridPosition playerEnd {};
        bool movedRock = false;
        size_t rockIndex = 0;
        GridPosition rockStart {};
        GridPosition rockEnd {};
    };

    void loadCurrentScreen();
    void advanceScreen();
    void update(float dt);
    void queuePressedCommands();
    void advancePlayerMovement(float dt);
    [[nodiscard]] bool completeActiveMove();
    [[nodiscard]] bool tryStartNextMove();
    [[nodiscard]] bool tryStartHeldMove();
    [[nodiscard]] bool tryStartMove(MoveDirection direction);
    [[nodiscard]] bool tryStartUndoMove();
    [[nodiscard]] MoveRecord invertMoveRecord(const MoveRecord& record) const;
    [[nodiscard]] GridPosition movementTarget(MoveDirection direction) const;
    [[nodiscard]] GridPosition movementTarget(GridPosition origin, MoveDirection direction) const;
    [[nodiscard]] Rock* rockAt(GridPosition position);
    [[nodiscard]] const Rock* rockAt(GridPosition position) const;
    [[nodiscard]] bool canMoveRock(GridPosition position, MoveDirection direction) const;
    [[nodiscard]] bool allPressurePlatesActive() const;
    [[nodiscard]] bool isEndUnlocked() const;
    [[nodiscard]] std::filesystem::path screenPath(int levelIndex, int screenIndex) const;
    [[nodiscard]] bool screenExists(int levelIndex, int screenIndex) const;
    [[nodiscard]] RenderFrameData buildRenderFrame() const;

    Window window_;
    VulkanRenderer renderer_;
    std::filesystem::path assetRoot_;
    Level level_;
    int currentLevel_ = 0;
    int currentScreen_ = 0;
    InputState input_;
    FrameTimer frameTimer_;
    GridPosition playerCell_ {};
    Vec2 playerRenderPosition_ {};
    std::vector<Rock> rocks_;
    std::deque<MoveCommand> pendingCommands_;
    std::vector<MoveRecord> moveHistory_;
    std::optional<size_t> undoCursor_;
    MoveRecord activeMove_;
    GridPosition moveStart_ {};
    GridPosition moveTarget_ {};
    Rock* movingRock_ = nullptr;
    GridPosition rockMoveStart_ {};
    GridPosition rockMoveTarget_ {};
    float moveElapsed_ = 0.0f;
    bool moving_ = false;
    bool running_ = true;
};

} // namespace sokoban
