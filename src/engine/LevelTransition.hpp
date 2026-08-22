#pragma once

namespace sokoban {

// Two-sided world transition. The midpoint is deliberately held for one
// update so Application can replace the world while the shader fully covers
// the scene, even when a long frame crosses the closing duration.
class LevelTransition {
public:
    struct UpdateResult {
        bool midpointReached = false;
        bool finished = false;
    };

    static constexpr float closingDurationSeconds = 0.30f;
    static constexpr float openingDurationSeconds = 0.38f;

    [[nodiscard]] bool start();
    [[nodiscard]] UpdateResult update(float dt);

    [[nodiscard]] bool active() const { return phase_ != Phase::Idle; }
    [[nodiscard]] float amount() const;

private:
    enum class Phase {
        Idle,
        Closing,
        Opening,
    };

    Phase phase_ = Phase::Idle;
    float elapsedSeconds_ = 0.0f;
};

} // namespace sokoban
