#pragma once

#include "engine/Math.hpp"

#include <optional>
#include <vector>

namespace sokoban {

class UiContext;
struct UiRect;

struct LevelCompleteStats {
    int level = 0;
    int moves = 0;
    double timeSeconds = 0.0;
    // Bests before this completion; empty on the first clear.
    std::optional<int> previousBestMoves;
    std::optional<double> previousBestTimeSeconds;
    bool newBestMoves = false;
    bool newBestTime = false;
    bool hasNextLevel = false;
};

struct GameCompleteLevelStats {
    std::optional<int> bestMoves;
    std::optional<double> bestTimeSeconds;
};

struct LevelCompleteInput {
    bool up = false;
    bool down = false;
    bool confirm = false;
};

struct LevelCompleteResult {
    bool continueRequested = false;
    bool titleRequested = false;
    bool levelSelectRequested = false;
};

// Headless end-of-level celebration state: shows moves/time against the
// previous personal bests and offers continuing or returning to the title.
// Finishing the final level opens the game-complete mode instead: a
// congratulations screen with per-level and whole-game best stats, offering
// Level Select (unlocked from then on) or the title.
class LevelCompleteOverlay {
public:
    enum class Mode {
        Level,
        Game,
    };

    void open(const LevelCompleteStats& stats);
    // levels holds one entry per level, in order; totals are derived.
    void openGameComplete(std::vector<GameCompleteLevelStats> levels);
    void close();
    [[nodiscard]] bool isOpen() const { return open_; }
    [[nodiscard]] Mode mode() const { return mode_; }
    [[nodiscard]] const LevelCompleteStats& stats() const { return stats_; }
    [[nodiscard]] int selectedRow() const { return selectedRow_; }

    [[nodiscard]] LevelCompleteResult draw(
        UiContext& ui,
        Vec2 viewport,
        const LevelCompleteInput& input);

private:
    [[nodiscard]] LevelCompleteResult drawLevelComplete(
        UiContext& ui,
        Vec2 viewport,
        const LevelCompleteInput& input);
    [[nodiscard]] LevelCompleteResult drawGameComplete(
        UiContext& ui,
        Vec2 viewport,
        const LevelCompleteInput& input);

    bool open_ = false;
    Mode mode_ = Mode::Level;
    int selectedRow_ = 0;
    LevelCompleteStats stats_ {};
    std::vector<GameCompleteLevelStats> gameLevels_;
};

} // namespace sokoban
