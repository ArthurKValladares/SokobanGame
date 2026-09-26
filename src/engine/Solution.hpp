#pragma once

#include "engine/EntityId.hpp"
#include "engine/GameplayLoop.hpp"
#include "engine/GameplayPresentation.hpp"
#include "engine/GameplaySession.hpp"
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
    GridPosition3 cell {};
    bool dead = false;
    bool fallen = false;
    bool drowned = false;

    bool operator==(const EntityChange&) const = default;
};

struct Step {
    Input input = Input::Up;
    std::vector<EntityChange> changes;

    bool operator==(const Step&) const = default;
};

struct Solution {
    std::uint64_t levelDigest = 0;
    // Where it was recorded, for people reading the file. Lookup uses the
    // digest.
    std::string recordedFor;
    std::vector<Step> steps;

    bool operator==(const Solution&) const = default;
};

// Digest of what gameplay depends on: layers, water layer and character.
// Decorations and selectors do not affect a solution and are left out.
[[nodiscard]] std::uint64_t levelDigest(const Level::Definition& definition);
[[nodiscard]] std::string digestText(std::uint64_t digest);

[[nodiscard]] std::string serialize(const Solution& solution);
// Throws std::runtime_error naming the line on malformed input.
[[nodiscard]] Solution parse(std::string_view text);

// `solutions/level<L>-screen<S>.solution` style name for a new recording.
[[nodiscard]] std::string fileNameFor(LevelLocation location);

// Drives one screen through GameplayLoop, one input at a time, waiting after
// each until nothing is moving. Waiting is what makes a recording replayable:
// a player pressing keys mid-slide depends on timing, a replay must not.
class Driver {
public:
    explicit Driver(const Level& level);

    // Tools only: continue from an arbitrary idle state (solution search).
    void resetTo(const GameState& state, EntityId activeHeroController);

    // Returns false when the world did not come to rest within the time
    // limit (an endless belt loop, say).
    [[nodiscard]] bool apply(Input input);

    [[nodiscard]] const GameState& state() const { return session_.state(); }
    [[nodiscard]] EntityId activeHeroController() const
    {
        return session_.activeHeroController();
    }
    [[nodiscard]] bool solved() const;
    [[nodiscard]] bool anyPlayerDead() const;

private:
    [[nodiscard]] bool settled() const;
    [[nodiscard]] bool runUntilSettled();

    const Level& level_;
    GameplaySession session_;
    GameplayPresentation presentation_;
};

[[nodiscard]] std::vector<EntityChange> changesBetween(
    const GameState& before, const GameState& after);

struct Recording {
    bool solved = false;
    Solution solution;
    std::string error;
};

// Plays `inputs` from the level's opening state and records what each one
// changed. `solved` only when the final input solves the screen; stopping
// short or solving early is reported in `error`.
[[nodiscard]] Recording record(
    const Level& level,
    const Level::Definition& definition,
    std::span<const Input> inputs,
    std::string recordedFor);

struct ReplayReport {
    bool passed = false;
    // One readable sentence: where replay first diverged, or that it passed.
    std::string message;
};

[[nodiscard]] ReplayReport replay(
    const Level& level, const Solution& solution);

} // namespace sokoban::solution
