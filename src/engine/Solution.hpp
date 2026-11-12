#pragma once

#include "engine/AnimationCatalog.hpp"
#include "engine/EntityId.hpp"
#include "engine/GameplayLoop.hpp"
#include "engine/Level.hpp"
#include "engine/LevelLocation.hpp"
#include "engine/Math.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Recorded puzzle solutions and their replay.
//
// A solution is the list of inputs that solves one screen, plus what each
// input changed. Replaying it through the real gameplay loop after a rule
// change shows exactly where a puzzle stopped behaving as it did: the step,
// and the first entity that ended up somewhere else.
//
// Solutions live in `solutions/` at the repository root rather than beside
// the screen, because level directories only admit screen and metadata files.
// A solution names the gameplay content it was recorded against by digest,
// so renumbering screens keeps it attached and editing a screen detaches it.
namespace sokoban::solution {

using Input = PlayerInput;

[[nodiscard]] std::string_view inputName(Input input);
[[nodiscard]] std::optional<Input> inputFromName(std::string_view name);

// What one entity looks like after a step, for entities the step changed.
struct EntityChange {
    enum class Kind : std::uint8_t {
        Player,
        Movable,
        Enemy,
    };

    Kind kind = Kind::Player;
    EntityId id = invalidEntityId;
    GridPosition3 cell { };
    bool dead = false;
    bool fallen = false;
    bool drowned = false;

    bool operator==(const EntityChange&) const = default;
};

struct Step {
    Input input = Input::Up;
    std::vector<EntityChange> changes;
    // Complete settled state, including devices, momentum, identities and
    // turns. The readable entity changes above are retained for movement
    // diagnostics.
    GameState state;
    EntityId activeHeroController = invalidEntityId;
    bool automaticMotionPaused = false;

    bool operator==(const Step&) const = default;
};

struct TimedFrame {
    GameplayLoop::InputFrame input;
    float elapsedSeconds = 0.0f;

    bool operator==(const TimedFrame&) const = default;
};

struct TimedRun {
    std::vector<TimedFrame> frames;
    std::size_t inputCount = 0;
    GameState finalState;
    EntityId activeHeroController = invalidEntityId;
    bool automaticMotionPaused = false;
    // Older timed recordings came from editor drafts, which finish the frame.
    // Campaign play ends the frame at the winning commit.
    bool playingDraft = true;

    bool operator==(const TimedRun&) const = default;
};

struct Solution {
    std::uint64_t levelDigest = 0;
    // Where it was recorded, for people reading the file. Lookup uses the
    // digest.
    std::string recordedFor;
    std::vector<Step> steps;
    std::optional<TimedRun> timed;

    [[nodiscard]] std::size_t stepCount() const
    {
        return timed ? timed->inputCount : steps.size();
    }

    bool operator==(const Solution&) const = default;
};

// Digest of what gameplay depends on: layers, water layer, character and,
// when present, gate/rotator/lock links, elevator links and stops, minecart
// links and direction, object/portal groups and plates authored beneath units.
// Decorations and selectors do not affect a solution and are left out.
[[nodiscard]] std::uint64_t levelDigest(const Level::Definition& definition);
[[nodiscard]] std::string digestText(std::uint64_t digest);

[[nodiscard]] std::string serialize(const Solution& solution);
// Throws std::runtime_error naming the line on malformed input.
[[nodiscard]] Solution parse(std::string_view text);

// `solutions/level<L>-screen<S>.solution` style name for a new recording.
[[nodiscard]] std::string fileNameFor(LevelLocation location);

// Replays legacy format-2 recordings, whose discrete inputs have no timing
// information. It advances between mechanical completion boundaries and waits
// for each step to settle. New recordings use the timed GameplayLoop instead.
class Driver {
public:
    explicit Driver(const Level& level);

    // Returns false when the world did not come to rest within the time
    // limit (an endless belt loop, say).
    [[nodiscard]] bool apply(Input input);

    [[nodiscard]] const GameState& state() const { return session_.state(); }
    [[nodiscard]] EntityId activeHeroController() const
    {
        return session_.activeHeroController();
    }
    [[nodiscard]] bool automaticMotionPaused() const
    {
        return session_.automaticMotionPaused();
    }
    [[nodiscard]] bool solved() const;
    [[nodiscard]] bool anyPlayerDead() const;

private:
    [[nodiscard]] bool settled() const;
    [[nodiscard]] bool runUntilSettled();

    const Level& level_;
    GameplaySession session_;
};

[[nodiscard]] std::vector<EntityChange> changesBetween(
    const GameState& before,
    const GameState& after);

struct Recording {
    bool solved = false;
    Solution solution;
    std::string error;
};

// Builds legacy format-2 fixtures from discrete inputs. The final input must
// solve the screen. New gameplay recordings use recordTimed.
[[nodiscard]] Recording record(
    const Level& level,
    const Level::Definition& definition,
    std::span<const Input> inputs,
    std::string recordedFor);

// Replays the controls and simulation deltas from the authored start through
// the first winning frame, including inputs during concurrent board motion.
[[nodiscard]] Recording recordTimed(
    const Level& level,
    const Level::Definition& definition,
    std::span<const TimedFrame> frames,
    std::string recordedFor,
    const AnimationCatalog* animations = nullptr,
    bool playingDraft = true);

// Timing of some actions depends on the authored animation catalog. Headless
// fixtures without an assets directory use default presentation timings.
[[nodiscard]] std::optional<AnimationCatalog> loadReplayAnimationCatalog(
    const std::filesystem::path& assetsRoot);

struct ReplayReport {
    bool passed = false;
    // One readable sentence: where replay first diverged, or that it passed.
    std::string message;
};

[[nodiscard]] ReplayReport replay(const Level& level, const Solution& solution,
    const AnimationCatalog* animations = nullptr);

} // namespace sokoban::solution
