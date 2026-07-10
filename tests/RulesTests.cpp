// Headless tests for the gameplay rules engine. No SDL/Vulkan dependencies:
// this file compiles against Level, TileTypes, and Rules only.

#include "engine/Level.hpp"
#include "engine/Rules.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace sokoban;

int failures = 0;
int checks = 0;
const char* currentTest = "";

void checkImpl(bool ok, const char* expression, int line)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << "FAIL [" << currentTest << "] line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)

#define TEST(name) currentTest = name

Level makeLevel(const std::vector<std::vector<std::string>>& layers)
{
    std::vector<std::string> lines;
    for (size_t layer = 0; layer < layers.size(); ++layer) {
        lines.push_back("@layer " + std::to_string(layer));
        lines.insert(lines.end(), layers[layer].begin(), layers[layer].end());
        lines.emplace_back();
    }
    return Level::loadFromLines(lines, "test level");
}

GridPosition3 cell(int x, int y, int z)
{
    return { x, y, z };
}

void testInitialState()
{
    TEST("initialState");
    const Level level = makeLevel({
        { "...." },
        { "CRI " },
    });
    const GameState state = rules::initialState(level);

    CHECK(state.player == cell(0, 0, 1));
    CHECK(!state.playerDead);
    CHECK(state.movables.size() == 2);
    CHECK(state.movables[0].type == TileType::Rock);
    CHECK(state.movables[0].cell == cell(1, 0, 1));
    CHECK(!state.movables[0].fallen);
    CHECK(state.movables[1].type == TileType::Ice);
    CHECK(state.movables[1].cell == cell(2, 0, 1));

    // Movable cells become Air in the static level.
    CHECK(level.tileAt(1, 0, 1) == TileType::Air);
    CHECK(level.tileAt(2, 0, 1) == TileType::Air);
}

void testSimpleMoveAndWall()
{
    TEST("simpleMoveAndWall");
    const Level level = makeLevel({
        { "..." },
        { " C#" },
    });
    const GameState state = rules::initialState(level);

    const auto left = rules::tryMove(level, state, MoveDirection::Left);
    CHECK(left.has_value());
    CHECK(left->player == cell(0, 0, 1));
    CHECK(!left->playerDead);

    const auto right = rules::tryMove(level, state, MoveDirection::Right);
    CHECK(!right.has_value());

    const auto up = rules::tryMove(level, state, MoveDirection::Up);
    CHECK(!up.has_value());
}

void testTryMoveIsPure()
{
    TEST("tryMoveIsPure");
    const Level level = makeLevel({
        { "..." },
        { "CR " },
    });
    const GameState state = rules::initialState(level);
    const GameState snapshot = state;

    const auto moved = rules::tryMove(level, state, MoveDirection::Right);
    CHECK(moved.has_value());
    CHECK(state == snapshot);
}

void testPushRock()
{
    TEST("pushRock");
    const Level level = makeLevel({
        { "...." },
        { "CR  " },
    });
    const GameState state = rules::initialState(level);

    const auto moved = rules::tryMove(level, state, MoveDirection::Right);
    CHECK(moved.has_value());
    CHECK(moved->player == cell(1, 0, 1));
    CHECK(moved->movables[0].cell == cell(2, 0, 1));
    CHECK(!moved->movables[0].fallen);
}

void testPushBlocked()
{
    TEST("pushBlocked");
    const Level wallBehind = makeLevel({
        { "..." },
        { "CR#" },
    });
    CHECK(!rules::tryMove(wallBehind, rules::initialState(wallBehind), MoveDirection::Right).has_value());

    const Level rockBehind = makeLevel({
        { "...." },
        { "CRR " },
    });
    CHECK(!rules::tryMove(rockBehind, rules::initialState(rockBehind), MoveDirection::Right).has_value());
}

void testConveyorCannotPush()
{
    TEST("conveyorCannotPush");
    const Level level = makeLevel({
        { "..." },
        { "CR " },
    });
    const GameState state = rules::initialState(level);

    // allowPush = false models conveyor-driven movement.
    CHECK(!rules::tryMove(level, state, MoveDirection::Right, false).has_value());
    CHECK(rules::tryMove(level, state, MoveDirection::Right, true).has_value());
}

void testConveyorDirection()
{
    TEST("conveyorDirection");
    const Level level = makeLevel({
        { ".." },
        { "C>" },
    });

    CHECK(rules::conveyorDirectionAt(level, cell(1, 0, 1)) == MoveDirection::Right);
    CHECK(rules::conveyorDirectionAt(level, cell(0, 0, 1)) == std::nullopt);
    CHECK(rules::conveyorDirectionAt(level, cell(9, 0, 1)) == std::nullopt);

    // The player can step onto a conveyor tile like any passable cell.
    const auto moved = rules::tryMove(level, rules::initialState(level), MoveDirection::Right);
    CHECK(moved.has_value());
    CHECK(moved->player == cell(1, 0, 1));
}

void testPressurePlateUnlocksEnd()
{
    TEST("pressurePlateUnlocksEnd");
    const Level level = makeLevel({
        { "...." },
        { "CRPE" },
    });
    GameState state = rules::initialState(level);
    CHECK(!rules::isEndUnlocked(level, state));

    const auto pushed = rules::tryMove(level, state, MoveDirection::Right);
    CHECK(pushed.has_value());
    CHECK(pushed->movables[0].cell == cell(2, 0, 1));
    CHECK(rules::isEndUnlocked(level, *pushed));
    CHECK(!rules::isAtUnlockedEnd(level, *pushed));
}

void testPlayerOnPlateUnlocks()
{
    TEST("playerOnPlateUnlocks");
    const Level level = makeLevel({
        { ".." },
        { "CP" },
    });
    const auto moved = rules::tryMove(level, rules::initialState(level), MoveDirection::Right);
    CHECK(moved.has_value());
    CHECK(rules::isEndUnlocked(level, *moved));
}

void testPlayerDrownsInWater()
{
    TEST("playerDrownsInWater");
    const Level level = makeLevel({
        { ".W" },
        { "C " },
    });
    const GameState state = rules::initialState(level);
    CHECK(rules::isUnfilledWater(level, state, cell(1, 0, 1)));

    const auto moved = rules::tryMove(level, state, MoveDirection::Right);
    CHECK(moved.has_value());
    CHECK(moved->player == cell(1, 0, 1));
    CHECK(moved->playerDead);

    // A dead player cannot move; drowned player fills the water.
    CHECK(!rules::tryMove(level, *moved, MoveDirection::Left).has_value());
    CHECK(!rules::isUnfilledWater(level, *moved, cell(1, 0, 1)));
}

void testRockFillsWater()
{
    TEST("rockFillsWater");
    const Level level = makeLevel({
        { "..W." },
        { "CR  " },
    });
    GameState state = rules::initialState(level);

    const auto pushed = rules::tryMove(level, state, MoveDirection::Right);
    CHECK(pushed.has_value());
    CHECK(pushed->movables[0].cell == cell(2, 0, 1));
    CHECK(pushed->movables[0].fallen);
    CHECK(!rules::isUnfilledWater(level, *pushed, cell(2, 0, 1)));

    // The filled water is now safe to walk over.
    const auto walked = rules::tryMove(level, *pushed, MoveDirection::Right);
    CHECK(walked.has_value());
    CHECK(walked->player == cell(2, 0, 1));
    CHECK(!walked->playerDead);
}

void testIceBlockSlides()
{
    TEST("iceBlockSlides");
    const Level level = makeLevel({
        { "....." },
        { "CI  #" },
    });
    const auto moved = rules::tryMove(level, rules::initialState(level), MoveDirection::Right);
    CHECK(moved.has_value());
    CHECK(moved->player == cell(1, 0, 1));
    // The ice block slides until it hits the wall.
    CHECK(moved->movables[0].cell == cell(3, 0, 1));
    CHECK(!moved->movables[0].fallen);
}

void testIceBlockSlidesIntoWater()
{
    TEST("iceBlockSlidesIntoWater");
    const Level level = makeLevel({
        { "...W." },
        { "CI   " },
    });
    const auto moved = rules::tryMove(level, rules::initialState(level), MoveDirection::Right);
    CHECK(moved.has_value());
    CHECK(moved->movables[0].cell == cell(3, 0, 1));
    CHECK(moved->movables[0].fallen);
}

void testPlayerSlidesOnFallenIce()
{
    TEST("playerSlidesOnFallenIce");
    const Level level = makeLevel({
        { "...W." },
        { "CI   " },
    });
    GameState state = rules::initialState(level);
    state = *rules::tryMove(level, state, MoveDirection::Right); // ice falls into water at x=3
    state = *rules::tryMove(level, state, MoveDirection::Right); // player to x=2

    // Stepping onto the ice-filled water slides the player across it.
    const auto slid = rules::tryMove(level, state, MoveDirection::Right);
    CHECK(slid.has_value());
    CHECK(slid->player == cell(4, 0, 1));
    CHECK(!slid->playerDead);
}

void testConveyorStepMovesRock()
{
    TEST("conveyorStepMovesRock");
    const Level level = makeLevel({
        { "....." },
        { "C> R " },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(1, 0, 1); // place the rock on the conveyor

    CHECK(rules::anyEntityOnConveyor(level, state));
    const auto stepped = rules::applyConveyorStep(level, state);
    CHECK(stepped.has_value());
    CHECK(stepped->movables[0].cell == cell(2, 0, 1));
    CHECK(stepped->player == cell(0, 0, 1)); // player not on a conveyor

    // Off the belt now: nothing left to convey.
    CHECK(!rules::anyEntityOnConveyor(level, *stepped));
}

void testConveyorStepChainsRocks()
{
    TEST("conveyorStepChainsRocks");
    const Level level = makeLevel({
        { "......" },
        { ">>>RRC" },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(1, 0, 1);
    state.movables[1].cell = cell(2, 0, 1);

    // The front rock vacates its cell this same tick, so both advance.
    const auto stepped = rules::applyConveyorStep(level, state);
    CHECK(stepped.has_value());
    CHECK(stepped->movables[0].cell == cell(2, 0, 1));
    CHECK(stepped->movables[1].cell == cell(3, 0, 1));
}

void testConveyorStepBlocked()
{
    TEST("conveyorStepBlocked");
    const Level level = makeLevel({
        { "...." },
        { ">#RC" },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(0, 0, 1); // on the conveyor, wall ahead

    CHECK(rules::anyEntityOnConveyor(level, state));
    CHECK(!rules::applyConveyorStep(level, state).has_value());
}

void testConveyorStepRockBlockedByPlayer()
{
    TEST("conveyorStepRockBlockedByPlayer");
    const Level level = makeLevel({
        { "..." },
        { ">CR" },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(0, 0, 1); // on the conveyor, player ahead

    CHECK(!rules::applyConveyorStep(level, state).has_value());
}

void testConveyorStepMovesPlayerAndRockTogether()
{
    TEST("conveyorStepMovesPlayerAndRockTogether");
    const Level level = makeLevel({
        { "....." },
        { ">>C R" },
    });
    GameState state = rules::initialState(level);
    state.player = cell(0, 0, 1);           // on the first conveyor
    state.movables[0].cell = cell(1, 0, 1); // on the second conveyor

    const auto stepped = rules::applyConveyorStep(level, state);
    CHECK(stepped.has_value());
    CHECK(stepped->movables[0].cell == cell(2, 0, 1));
    CHECK(stepped->player == cell(1, 0, 1)); // into the cell the rock vacated
    CHECK(!stepped->playerDead);
}

void testConveyorStepRockIntoWater()
{
    TEST("conveyorStepRockIntoWater");
    const Level level = makeLevel({
        { "..W." },
        { "C> R" },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(1, 0, 1);

    const auto stepped = rules::applyConveyorStep(level, state);
    CHECK(stepped.has_value());
    CHECK(stepped->movables[0].cell == cell(2, 0, 1));
    CHECK(stepped->movables[0].fallen);
    CHECK(!rules::isUnfilledWater(level, *stepped, cell(2, 0, 1)));
}

void testLadderClimb()
{
    TEST("ladderClimb");
    const Level level = makeLevel({
        { "..." },
        { ".LC" },
    });
    GameState state = rules::initialState(level);

    const auto ontoLadder = rules::tryMove(level, state, MoveDirection::Left);
    CHECK(ontoLadder.has_value());
    CHECK(ontoLadder->player == cell(1, 0, 1));

    // Moving toward the attached ground climbs on top of it.
    const auto climbed = rules::tryMove(level, *ontoLadder, MoveDirection::Left);
    CHECK(climbed.has_value());
    CHECK(climbed->player == cell(0, 0, 2));
    CHECK(!climbed->playerDead);
}

void testLadderClimbBlockedByMovable()
{
    TEST("ladderClimbBlockedByMovable");
    // A rock sits on top of the ground block the ladder is attached to.
    const Level blocked = makeLevel({
        { "..." },
        { ".LC" },
        { "R  " },
    });
    GameState state = rules::initialState(blocked);
    state = *rules::tryMove(blocked, state, MoveDirection::Left); // onto ladder

    // The climb destination (0, 0, 2) is occupied by the rock, and the flat
    // target is solid ground, so the move fails entirely.
    CHECK(!rules::tryMove(blocked, state, MoveDirection::Left).has_value());
}

void testFallToLowerLayer()
{
    TEST("fallToLowerLayer");
    // Documents current behavior: with no support below, entities fall until
    // they land on something or reach the bottom layer, where they rest.
    const Level level = makeLevel({
        { ".  " },
        { "C  " },
    });
    const auto moved = rules::tryMove(level, rules::initialState(level), MoveDirection::Right);
    CHECK(moved.has_value());
    CHECK(moved->player == cell(1, 0, 0));
    CHECK(!moved->playerDead);
}

} // namespace

int main()
{
    testInitialState();
    testSimpleMoveAndWall();
    testTryMoveIsPure();
    testPushRock();
    testPushBlocked();
    testConveyorCannotPush();
    testConveyorDirection();
    testConveyorStepMovesRock();
    testConveyorStepChainsRocks();
    testConveyorStepBlocked();
    testConveyorStepRockBlockedByPlayer();
    testConveyorStepMovesPlayerAndRockTogether();
    testConveyorStepRockIntoWater();
    testPressurePlateUnlocksEnd();
    testPlayerOnPlateUnlocks();
    testPlayerDrownsInWater();
    testRockFillsWater();
    testIceBlockSlides();
    testIceBlockSlidesIntoWater();
    testPlayerSlidesOnFallenIce();
    testLadderClimb();
    testLadderClimbBlockedByMovable();
    testFallToLowerLayer();

    if (failures == 0) {
        std::cout << "All " << checks << " checks passed.\n";
        return 0;
    }

    std::cerr << failures << " of " << checks << " checks failed.\n";
    return 1;
}
