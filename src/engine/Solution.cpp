#include "engine/Solution.hpp"

#include "engine/Character.hpp"
#include "engine/Rules.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sokoban::solution {
namespace {

constexpr std::array inputNames {
    std::pair { Input::Up, std::string_view("up") },
    std::pair { Input::Down, std::string_view("down") },
    std::pair { Input::Left, std::string_view("left") },
    std::pair { Input::Right, std::string_view("right") },
    std::pair { Input::CycleHero, std::string_view("cycle") },
    std::pair { Input::Interact, std::string_view("interact") },
    std::pair { Input::Undo, std::string_view("undo") },
};

// Two minutes of game time after one input is far beyond any settle this
// game has; running past it means the world never comes to rest.
constexpr float maximumSettleSeconds = 120.0f;
// Also guard zero-duration or otherwise non-advancing action cycles.
constexpr int maximumSettleTransitions = 10'000;

void hashBytes(std::uint64_t& hash, std::string_view bytes)
{
    for (const char byte : bytes) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 0x100000001b3ULL;
    }
}

char kindLetter(EntityChange::Kind kind)
{
    switch (kind) {
    case EntityChange::Kind::Player: return 'p';
    case EntityChange::Kind::Movable: return 'm';
    case EntityChange::Kind::Enemy: return 'e';
    }
    return '?';
}

std::string_view kindWord(EntityChange::Kind kind)
{
    switch (kind) {
    case EntityChange::Kind::Player: return "player";
    case EntityChange::Kind::Movable: return "movable";
    case EntityChange::Kind::Enemy: return "enemy";
    }
    return "entity";
}

std::string cellText(GridPosition3 cell)
{
    return "(" + std::to_string(cell.x) + ", " + std::to_string(cell.y) +
        ", " + std::to_string(cell.z) + ")";
}

std::string describe(const EntityChange& change)
{
    std::string text = cellText(change.cell);
    if (change.dead) {
        text += ", dead";
    }
    if (change.fallen) {
        text += ", fallen";
    }
    if (change.drowned) {
        text += ", drowned";
    }
    return text;
}

std::string label(const EntityChange& change)
{
    return std::string(kindWord(change.kind)) + " " +
        std::to_string(change.id);
}

std::vector<EntityChange> snapshotEntities(const GameState& state)
{
    std::vector<EntityChange> entities;
    entities.reserve(
        state.players.size() + state.movables.size() + state.enemies.size());
    for (const GameState::Player& player : state.players) {
        entities.push_back({
            .kind = EntityChange::Kind::Player,
            .id = player.id,
            .cell = player.cell,
            .dead = player.dead,
            .drowned = player.drowned,
        });
    }
    for (const GameState::Movable& movable : state.movables) {
        entities.push_back({
            .kind = EntityChange::Kind::Movable,
            .id = movable.id,
            .cell = movable.cell,
            .dead = movable.dead,
            .fallen = movable.fallen,
        });
    }
    for (const GameState::Enemy& enemy : state.enemies) {
        entities.push_back({
            .kind = EntityChange::Kind::Enemy,
            .id = enemy.id,
            .cell = enemy.cell,
            .dead = enemy.dead,
            .fallen = enemy.fallen,
        });
    }
    return entities;
}

int parseInt(std::string_view text, std::size_t line)
{
    int value = 0;
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc {} || end != text.data() + text.size()) {
        throw std::runtime_error(
            "solution line " + std::to_string(line) + ": '" +
            std::string(text) + "' is not a number");
    }
    return value;
}

// `m7=3,4,1+dead+fallen`
EntityChange parseChange(std::string_view token, std::size_t line)
{
    const auto fail = [&](const std::string& why) -> EntityChange {
        throw std::runtime_error(
            "solution line " + std::to_string(line) + ": change '" +
            std::string(token) + "' " + why);
    };
    const std::size_t equals = token.find('=');
    if (token.size() < 4 || equals == std::string_view::npos || equals < 2) {
        return fail("must look like m7=3,4,1");
    }
    EntityChange change;
    switch (token.front()) {
    case 'p': change.kind = EntityChange::Kind::Player; break;
    case 'm': change.kind = EntityChange::Kind::Movable; break;
    case 'e': change.kind = EntityChange::Kind::Enemy; break;
    default: return fail("has an unknown entity kind");
    }
    change.id = static_cast<EntityId>(
        parseInt(token.substr(1, equals - 1), line));
    std::string_view rest = token.substr(equals + 1);
    std::string_view flags;
    if (const std::size_t plus = rest.find('+');
        plus != std::string_view::npos) {
        flags = rest.substr(plus);
        rest = rest.substr(0, plus);
    }
    std::array<int, 3> coordinates {};
    for (std::size_t axis = 0; axis < coordinates.size(); ++axis) {
        const std::size_t comma = rest.find(',');
        if ((axis < 2) == (comma == std::string_view::npos)) {
            return fail("needs three coordinates");
        }
        coordinates[axis] = parseInt(rest.substr(0, comma), line);
        rest = comma == std::string_view::npos
            ? std::string_view {}
            : rest.substr(comma + 1);
    }
    change.cell = { coordinates[0], coordinates[1], coordinates[2] };
    while (!flags.empty()) {
        flags.remove_prefix(1);
        const std::size_t next = flags.find('+');
        const std::string_view flag = flags.substr(0, next);
        if (flag == "dead") {
            change.dead = true;
        } else if (flag == "fallen") {
            change.fallen = true;
        } else if (flag == "drowned") {
            change.drowned = true;
        } else {
            return fail("has an unknown flag");
        }
        flags = next == std::string_view::npos
            ? std::string_view {}
            : flags.substr(next);
    }
    return change;
}

std::string changeToken(const EntityChange& change)
{
    std::string token(1, kindLetter(change.kind));
    token += std::to_string(change.id) + "=" +
        std::to_string(change.cell.x) + "," +
        std::to_string(change.cell.y) + "," +
        std::to_string(change.cell.z);
    if (change.dead) {
        token += "+dead";
    }
    if (change.fallen) {
        token += "+fallen";
    }
    if (change.drowned) {
        token += "+drowned";
    }
    return token;
}

} // namespace

std::string_view inputName(Input input)
{
    for (const auto& [candidate, name] : inputNames) {
        if (candidate == input) {
            return name;
        }
    }
    return "?";
}

std::optional<Input> inputFromName(std::string_view name)
{
    for (const auto& [input, candidate] : inputNames) {
        if (candidate == name) {
            return input;
        }
    }
    return std::nullopt;
}

std::uint64_t levelDigest(const Level::Definition& definition)
{
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (std::size_t z = 0; z < definition.layers.size(); ++z) {
        hashBytes(hash, "@layer " + std::to_string(z) + "\n");
        for (const std::string& row : definition.layers[z]) {
            // Trailing air is not content; editors may trim or pad it.
            const std::size_t end = row.find_last_not_of(' ');
            hashBytes(
                hash,
                end == std::string::npos ? std::string_view {}
                                         : std::string_view(row).substr(0, end + 1));
            hashBytes(hash, "\n");
        }
    }
    hashBytes(
        hash,
        definition.waterLayer
            ? "@water " + std::to_string(*definition.waterLayer)
            : std::string("@water none"));
    hashBytes(
        hash,
        definition.character
            ? std::string(characterTypeName(*definition.character))
            : std::string("default"));
    // Rotator links decide which plates turn which units. Hashed only when
    // present so digests of screens without rotators stay unchanged.
    std::vector<Level::Rotator> rotators = definition.rotators;
    const auto cellOrder = [](GridPosition3 cell) {
        return std::array { cell.z, cell.y, cell.x };
    };
    std::ranges::sort(rotators, {}, [&](const Level::Rotator& rotator) {
        return cellOrder(rotator.cell);
    });
    for (Level::Rotator& rotator : rotators) {
        std::ranges::sort(rotator.pressurePlates, {}, cellOrder);
        std::string text = "@rotator " + std::to_string(rotator.cell.x) +
            "," + std::to_string(rotator.cell.y) + "," +
            std::to_string(rotator.cell.z) + ":";
        for (GridPosition3 plate : rotator.pressurePlates) {
            text += " " + std::to_string(plate.x) + "," +
                std::to_string(plate.y) + "," + std::to_string(plate.z);
        }
        hashBytes(hash, text + "\n");
    }
    // Elevator links and stops decide which plates move which platforms and
    // where to; hashed only when present, like rotators.
    std::vector<Level::Elevator> elevators = definition.elevators;
    std::ranges::sort(elevators, {}, [&](const Level::Elevator& elevator) {
        return cellOrder(elevator.cell);
    });
    for (Level::Elevator& elevator : elevators) {
        std::ranges::sort(elevator.pressurePlates, {}, cellOrder);
        std::string text = "@elevator " + std::to_string(elevator.cell.x) +
            "," + std::to_string(elevator.cell.y) + "," +
            std::to_string(elevator.cell.z) + ":";
        for (GridPosition3 plate : elevator.pressurePlates) {
            text += " " + std::to_string(plate.x) + "," +
                std::to_string(plate.y) + "," + std::to_string(plate.z);
        }
        text += " levels";
        // Stop order is travel order, so it is hashed as authored.
        for (const int stop : elevator.levels) {
            text += " " + std::to_string(stop);
        }
        hashBytes(hash, text + "\n");
    }
    std::vector<Level::Minecart> minecarts = definition.minecarts;
    std::ranges::sort(minecarts, {}, [&](const Level::Minecart& minecart) {
        return cellOrder(minecart.cell);
    });
    for (Level::Minecart& minecart : minecarts) {
        std::ranges::sort(minecart.pressurePlates, {}, cellOrder);
        std::string text = "@minecart " + std::to_string(minecart.cell.x) +
            "," + std::to_string(minecart.cell.y) + "," +
            std::to_string(minecart.cell.z) + ":";
        for (GridPosition3 plate : minecart.pressurePlates) {
            text += " " + std::to_string(plate.x) + "," +
                std::to_string(plate.y) + "," + std::to_string(plate.z);
        }
        text += " direction " + std::to_string(minecart.initialDirection);
        hashBytes(hash, text + "\n");
    }
    // Plates authored beneath units change what the screen does from its
    // first step; hashed only when present, like rotators.
    std::vector<Level::Plate> plates = definition.plates;
    std::ranges::sort(plates, {}, [&](const Level::Plate& plate) {
        return cellOrder(plate.cell);
    });
    for (const Level::Plate& plate : plates) {
        hashBytes(
            hash,
            "@plate " + std::to_string(plate.cell.x) + "," +
                std::to_string(plate.cell.y) + "," +
                std::to_string(plate.cell.z) + ":" +
                std::string(tileTypeName(plate.tile)) + "\n");
    }
    return hash;
}

std::string digestText(std::uint64_t digest)
{
    std::array<char, 17> text {};
    std::snprintf(
        text.data(), text.size(), "%016llx",
        static_cast<unsigned long long>(digest));
    return std::string(text.data());
}

std::string serialize(const Solution& solution)
{
    std::string text =
        "# Sokoban 3D recorded solution. Replayed by the solution_replay "
        "test;\n# see README.md > Solutions. One input per step, then "
        "what it changed.\n";
    text += "format 1\n";
    text += "level-digest " + digestText(solution.levelDigest) + "\n";
    if (!solution.recordedFor.empty()) {
        text += "recorded-for " + solution.recordedFor + "\n";
    }
    for (const Step& step : solution.steps) {
        text += "step ";
        text += inputName(step.input);
        for (const EntityChange& change : step.changes) {
            text += " " + changeToken(change);
        }
        text += "\n";
    }
    return text;
}

Solution parse(std::string_view text)
{
    Solution solution;
    bool sawFormat = false;
    bool sawDigest = false;
    std::size_t lineNumber = 0;
    std::istringstream stream { std::string(text) };
    std::string line;
    while (std::getline(stream, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::istringstream words(line);
        std::string keyword;
        words >> keyword;
        const auto fail = [&](const std::string& why) {
            throw std::runtime_error(
                "solution line " + std::to_string(lineNumber) + ": " + why);
        };
        if (keyword == "format") {
            std::string version;
            words >> version;
            if (version != "1") {
                fail("unsupported format '" + version + "'");
            }
            sawFormat = true;
        } else if (keyword == "level-digest") {
            std::string digest;
            words >> digest;
            std::uint64_t value = 0;
            const auto [end, error] = std::from_chars(
                digest.data(), digest.data() + digest.size(), value, 16);
            if (digest.size() != 16 || error != std::errc {} ||
                end != digest.data() + digest.size()) {
                fail("level-digest must be 16 hex digits");
            }
            solution.levelDigest = value;
            sawDigest = true;
        } else if (keyword == "recorded-for") {
            std::getline(words >> std::ws, solution.recordedFor);
        } else if (keyword == "step") {
            std::string name;
            words >> name;
            const std::optional<Input> input = inputFromName(name);
            if (!input) {
                fail("unknown input '" + name + "'");
            }
            Step step { .input = *input };
            std::string token;
            while (words >> token) {
                step.changes.push_back(parseChange(token, lineNumber));
            }
            solution.steps.push_back(std::move(step));
        } else {
            fail("unknown keyword '" + keyword + "'");
        }
    }
    if (!sawFormat || !sawDigest) {
        throw std::runtime_error(
            "solution is missing its format or level-digest line");
    }
    return solution;
}

std::string fileNameFor(LevelLocation location)
{
    return "level" + std::to_string(location.level) + "-screen" +
        std::to_string(location.screen) + ".solution";
}

Driver::Driver(const Level& level)
    : level_(level)
{
    session_.reset(level_);
}

void Driver::resetTo(const GameState& state, EntityId activeHeroController)
{
    session_.resetToState(state, activeHeroController);
}

bool Driver::settled() const
{
    // Pending motion counts only when it can run: after an undo, slides and
    // belts wait for the next step, and while a hero is dead nothing runs.
    return !session_.moving() &&
        (rules::anyPlayerDead(session_.state()) ||
            session_.automaticMotionPaused() ||
            !rules::hasPendingMotion(level_, session_.state()));
}

bool Driver::runUntilSettled()
{
    float elapsedSeconds = 0.0f;
    for (int transition = 0;
         transition < maximumSettleTransitions;
         ++transition) {
        // This is GameplayLoop's mechanical core without presentation work:
        // admit everything that can run concurrently, advance exactly to the
        // next completion, commit it, and repeat from the new world state.
        while (session_.tryStartNextAction(level_, {})) {
        }
        if (settled()) {
            return true;
        }
        if (!session_.moving()) {
            return false;
        }
        const float step = session_.timeToNextCompletion();
        elapsedSeconds += step;
        if (elapsedSeconds > maximumSettleSeconds) {
            return false;
        }
        session_.advanceActiveAction(step);
        if (!session_.anyActionComplete()) {
            return false;
        }
        session_.completeActiveAction();
    }
    return settled();
}

bool Driver::apply(Input input)
{
    switch (input) {
    case Input::Up: session_.queueMove(MoveDirection::Up); break;
    case Input::Down: session_.queueMove(MoveDirection::Down); break;
    case Input::Left: session_.queueMove(MoveDirection::Left); break;
    case Input::Right: session_.queueMove(MoveDirection::Right); break;
    case Input::CycleHero: session_.cycleActiveHero(); break;
    case Input::Interact: session_.queueMirror(); break;
    case Input::Undo: session_.queueUndo(); break;
    }
    return runUntilSettled();
}

bool Driver::solved() const
{
    return rules::isAtUnlockedEnd(level_, session_.state());
}

bool Driver::anyPlayerDead() const
{
    return rules::anyPlayerDead(session_.state());
}

std::vector<EntityChange> changesBetween(
    const GameState& before, const GameState& after)
{
    const std::vector<EntityChange> previous = snapshotEntities(before);
    std::vector<EntityChange> changes;
    for (const EntityChange& entity : snapshotEntities(after)) {
        const auto match = std::ranges::find_if(
            previous,
            [&](const EntityChange& candidate) {
                return candidate.kind == entity.kind &&
                    candidate.id == entity.id;
            });
        if (match == previous.end() || !(*match == entity)) {
            changes.push_back(entity);
        }
    }
    return changes;
}

Recording record(
    const Level& level,
    const Level::Definition& definition,
    std::span<const Input> inputs,
    std::string recordedFor)
{
    Recording recording;
    recording.solution.levelDigest = levelDigest(definition);
    recording.solution.recordedFor = std::move(recordedFor);
    Driver driver(level);
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const GameState before = driver.state();
        if (!driver.apply(inputs[index])) {
            recording.error = "step " + std::to_string(index + 1) +
                " never came to rest";
            return recording;
        }
        recording.solution.steps.push_back({
            .input = inputs[index],
            .changes = changesBetween(before, driver.state()),
        });
        if (driver.solved() && index + 1 < inputs.size()) {
            recording.error = "solved after step " +
                std::to_string(index + 1) + " of " +
                std::to_string(inputs.size());
            return recording;
        }
    }
    if (!driver.solved()) {
        recording.error = "the inputs do not solve the screen when each "
                          "one is allowed to finish before the next";
        return recording;
    }
    recording.solved = true;
    return recording;
}

ReplayReport replay(const Level& level, const Solution& solution)
{
    Driver driver(level);
    for (std::size_t index = 0; index < solution.steps.size(); ++index) {
        const Step& step = solution.steps[index];
        const std::string where = "step " + std::to_string(index + 1) +
            " of " + std::to_string(solution.steps.size()) + " (" +
            std::string(inputName(step.input)) + ")";
        const GameState before = driver.state();
        if (!driver.apply(step.input)) {
            return { .message = where + ": the world never came to rest" };
        }
        const std::vector<EntityChange> actual =
            changesBetween(before, driver.state());
        const std::vector<EntityChange> current =
            snapshotEntities(driver.state());
        for (const EntityChange& expected : step.changes) {
            const auto found = std::ranges::find_if(
                current,
                [&](const EntityChange& candidate) {
                    return candidate.kind == expected.kind &&
                        candidate.id == expected.id;
                });
            if (found == current.end()) {
                return {
                    .message = where + ": " + label(expected) +
                        " no longer exists",
                };
            }
            if (!(*found == expected)) {
                return {
                    .message = where + ": " + label(expected) +
                        " should be at " + describe(expected) +
                        " but is at " + describe(*found),
                };
            }
        }
        for (const EntityChange& change : actual) {
            const bool expected = std::ranges::any_of(
                step.changes,
                [&](const EntityChange& candidate) {
                    return candidate.kind == change.kind &&
                        candidate.id == change.id;
                });
            if (!expected) {
                return {
                    .message = where + ": " + label(change) +
                        " unexpectedly changed to " + describe(change),
                };
            }
        }
        if (driver.solved() && index + 1 < solution.steps.size()) {
            return {
                .message = where + ": the screen is already solved, " +
                    std::to_string(solution.steps.size() - index - 1) +
                    " steps early",
            };
        }
    }
    if (!driver.solved()) {
        return {
            .message = "every step matched, but the screen is not solved "
                       "at the end",
        };
    }
    return {
        .passed = true,
        .message = "solved in " + std::to_string(solution.steps.size()) +
            " steps",
    };
}

} // namespace sokoban::solution
