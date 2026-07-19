#pragma once

#include "engine/Math.hpp"

#include <optional>

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

struct LevelCompleteInput {
    bool up = false;
    bool down = false;
    bool confirm = false;
};

struct LevelCompleteResult {
    bool continueRequested = false;
    bool titleRequested = false;
};

// Headless end-of-level celebration state: shows moves/time against the
// previous personal bests and offers continuing or returning to the title.
class LevelCompleteOverlay {
public:
    void open(const LevelCompleteStats& stats);
    void close();
    [[nodiscard]] bool isOpen() const { return open_; }
    [[nodiscard]] const LevelCompleteStats& stats() const { return stats_; }
    [[nodiscard]] int selectedRow() const { return selectedRow_; }

    [[nodiscard]] LevelCompleteResult draw(
        UiContext& ui,
        Vec2 viewport,
        const LevelCompleteInput& input);

private:
    bool open_ = false;
    int selectedRow_ = 0;
    LevelCompleteStats stats_ {};
};

} // namespace sokoban
