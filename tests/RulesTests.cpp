// Headless tests for the step-based gameplay rules engine. No SDL/Vulkan
// dependencies: this file compiles against Level, TileTypes, and Rules only.

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
    CHECK(!state.playerSliding);
    CHECK(state.movables.size() == 2);
    CHECK(state.movables[0].type == TileType::Rock);
    CHECK(state.movables[0].cell == cell(1, 0, 1));
    CHECK(!state.movables[0].fallen);
    CHECK(!state.movables[0].sliding);
    CHECK(state.movables[1].type == TileType::Ice);
    CHECK(state.movables[1].cell == cell(2, 0, 1));

    // Movable cells become Air in the static level.
    CHECK(level.tileAt(1, 0, 1) == TileType::Air);
    CHECK(level.tileAt(2, 0, 1) == TileType::Air);
    CHECK(!rules::hasPendingMotion(level, state));
}

void testStepMovesPlayer()
{
    TEST("stepMovesPlayer");
    const Level level = makeLevel({
        { "..." },
        { " C#" },
    });
    const GameState state = rules::initialState(level);

    const GameState left = rules::step(level, state, MoveDirection::Left);
    CHECK(left.player == cell(0, 0, 1));
    CHECK(!left.playerDead);

    // Blocked by the wall / by the level edge: the step changes nothing.
    CHECK(rules::step(level, state, MoveDirection::Right) == state);
    CHECK(rules::step(level, state, MoveDirection::Up) == state);

    // No input and nothing pending: nothing happens.
    CHECK(rules::step(level, state) == state);
}

void testStepIsPure()
{
    TEST("stepIsPure");
    const Level level = makeLevel({
        { "..." },
        { "CR " },
    });
    const GameState state = rules::initialState(level);
    const GameState snapshot = state;

    const GameState moved = rules::step(level, state, MoveDirection::Right);
    CHECK(moved != state);
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

    // Push resolves within one step: player and rock advance together.
    const GameState pushed = rules::step(level, state, MoveDirection::Right);
    CHECK(pushed.player == cell(1, 0, 1));
    CHECK(pushed.movables[0].cell == cell(2, 0, 1));
    CHECK(!pushed.movables[0].fallen);
    // A rock on plain ground has no momentum: the world settles immediately.
    CHECK(!pushed.movables[0].sliding);
    CHECK(!rules::hasPendingMotion(level, pushed));
}

void testPushBlocked()
{
    TEST("pushBlocked");
    const Level wallBehind = makeLevel({
        { "..." },
        { "CR#" },
    });
    const GameState wallState = rules::initialState(wallBehind);
    CHECK(rules::step(wallBehind, wallState, MoveDirection::Right) == wallState);

    const Level rockBehind = makeLevel({
        { "...." },
        { "CRR " },
    });
    const GameState rockState = rules::initialState(rockBehind);
    CHECK(rules::step(rockBehind, rockState, MoveDirection::Right) == rockState);
}

void testIceSlidesOneTilePerStep()
{
    TEST("iceSlidesOneTilePerStep");
    const Level level = makeLevel({
        { "....." },
        { "CI  #" },
    });
    GameState state = rules::initialState(level);

    // Step 1: the push moves the ice one tile and gives it momentum.
    state = rules::step(level, state, MoveDirection::Right);
    CHECK(state.player == cell(1, 0, 1));
    CHECK(state.movables[0].cell == cell(2, 0, 1));
    CHECK(state.movables[0].sliding == MoveDirection::Right);
    CHECK(rules::hasPendingMotion(level, state));

    // Step 2: momentum carries it one more tile; the wall is next, so the
    // slide ends here.
    state = rules::step(level, state);
    CHECK(state.movables[0].cell == cell(3, 0, 1));
    CHECK(!state.movables[0].sliding);
    CHECK(!rules::hasPendingMotion(level, state));
}

void testPlayerMovesWhileIceSlides()
{
    TEST("playerMovesWhileIceSlides");
    const Level level = makeLevel({
        { "......", "......" },
        { "CI   #", "      " },
    });
    GameState state = rules::initialState(level);

    state = rules::step(level, state, MoveDirection::Right); // push
    CHECK(state.player == cell(1, 0, 1));
    CHECK(state.movables[0].cell == cell(2, 0, 1));
    CHECK(state.movables[0].sliding == MoveDirection::Right);

    // While the ice keeps sliding, the player walks somewhere else in the
    // very same step.
    state = rules::step(level, state, MoveDirection::Down);
    CHECK(state.player == cell(1, 1, 1));
    CHECK(state.movables[0].cell == cell(3, 0, 1));
    CHECK(state.movables[0].sliding == MoveDirection::Right);

    state = rules::step(level, state);
    CHECK(state.movables[0].cell == cell(4, 0, 1));
    CHECK(!state.movables[0].sliding); // wall ahead ends the slide
    CHECK(!rules::hasPendingMotion(level, state));
}

void testPlayerMovesWhileConveyorCarriesRock()
{
    TEST("playerMovesWhileConveyorCarriesRock");
    const Level level = makeLevel({
        { "....." },
        { "C> R " },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(1, 0, 1); // place the rock on the belt
    CHECK(rules::hasPendingMotion(level, state));

    // One step: the belt carries the rock while the player, on direct input,
    // walks into the cell the rock vacates.
    state = rules::step(level, state, MoveDirection::Right);
    CHECK(state.movables[0].cell == cell(2, 0, 1));
    CHECK(state.player == cell(1, 0, 1));
}

void testConveyorMovesRockEachStep()
{
    TEST("conveyorMovesRockEachStep");
    const Level level = makeLevel({
        { "....." },
        { "C>>R " },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(1, 0, 1);

    state = rules::step(level, state);
    CHECK(state.movables[0].cell == cell(2, 0, 1)); // still on a belt
    CHECK(rules::hasPendingMotion(level, state));

    state = rules::step(level, state);
    CHECK(state.movables[0].cell == cell(3, 0, 1)); // carried off the belt
    CHECK(!rules::hasPendingMotion(level, state));
    CHECK(state.player == cell(0, 0, 1)); // player never moved
}

void testConveyorBlocked()
{
    TEST("conveyorBlocked");
    const Level level = makeLevel({
        { "...." },
        { ">#RC" },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(0, 0, 1); // on the belt, wall ahead

    CHECK(rules::hasPendingMotion(level, state)); // it keeps trying
    CHECK(rules::step(level, state) == state);    // but nothing changes
}

void testConveyorRockBlockedByPlayer()
{
    TEST("conveyorRockBlockedByPlayer");
    const Level level = makeLevel({
        { "..." },
        { ">CR" },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(0, 0, 1); // on the belt, player ahead

    CHECK(rules::step(level, state) == state);
}

void testConveyorRockIntoWater()
{
    TEST("conveyorRockIntoWater");
    const Level level = makeLevel({
        { "..W." },
        { "C> R" },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(1, 0, 1);

    state = rules::step(level, state);
    CHECK(state.movables[0].cell == cell(2, 0, 1));
    CHECK(state.movables[0].fallen);
    CHECK(!state.movables[0].sliding);
    CHECK(!rules::isUnfilledWater(level, state, cell(2, 0, 1)));
}

void testIceIntoWater()
{
    TEST("iceIntoWater");
    const Level level = makeLevel({
        { "..W." },
        { "CI  " },
    });
    GameState state = rules::initialState(level);

    // The push lands the ice on the water cell; it falls in and the fall
    // cancels its momentum.
    state = rules::step(level, state, MoveDirection::Right);
    CHECK(state.movables[0].cell == cell(2, 0, 1));
    CHECK(state.movables[0].fallen);
    CHECK(!state.movables[0].sliding);
    CHECK(!rules::hasPendingMotion(level, state));
}

void testPlayerSlidesOnFallenIce()
{
    TEST("playerSlidesOnFallenIce");
    const Level level = makeLevel({
        { "...W." },
        { "CI   " },
    });
    GameState state = rules::initialState(level);

    state = rules::step(level, state, MoveDirection::Right); // push; ice slides
    CHECK(state.movables[0].sliding == MoveDirection::Right);
    state = rules::step(level, state); // ice reaches the water and falls in
    CHECK(state.movables[0].cell == cell(3, 0, 1));
    CHECK(state.movables[0].fallen);

    state = rules::step(level, state, MoveDirection::Right); // player to x=2
    CHECK(state.player == cell(2, 0, 1));

    // Stepping onto the ice-filled water gives the player slide momentum.
    state = rules::step(level, state, MoveDirection::Right);
    CHECK(state.player == cell(3, 0, 1));
    CHECK(state.playerSliding == MoveDirection::Right);
    CHECK(rules::hasPendingMotion(level, state));

    // Momentum carries the player off the ice, then ends on normal ground.
    state = rules::step(level, state);
    CHECK(state.player == cell(4, 0, 1));
    CHECK(!state.playerSliding);
    CHECK(!state.playerDead);
}

void testSlideMomentumOverridesInput()
{
    TEST("slideMomentumOverridesInput");
    const Level level = makeLevel({
        { "...W..", "......" },
        { "CI    ", "      " },
    });
    GameState state = rules::initialState(level);
    state = rules::step(level, state, MoveDirection::Right); // push
    state = rules::step(level, state);                       // ice falls into water
    state = rules::step(level, state, MoveDirection::Right); // player to x=2
    state = rules::step(level, state, MoveDirection::Right); // onto the ice floor
    CHECK(state.playerSliding == MoveDirection::Right);

    // Input cannot steer a sliding player: the slide continues instead.
    state = rules::step(level, state, MoveDirection::Down);
    CHECK(state.player == cell(4, 0, 1));
    CHECK(state.player.y == 0);
}

void testPressurePlateUnlocksEnd()
{
    TEST("pressurePlateUnlocksEnd");
    const Level level = makeLevel({
        { "...." },
        { "CRPE" },
    });
    const GameState state = rules::initialState(level);
    CHECK(!rules::isEndUnlocked(level, state));

    const GameState pushed = rules::step(level, state, MoveDirection::Right);
    CHECK(pushed.movables[0].cell == cell(2, 0, 1));
    CHECK(rules::isEndUnlocked(level, pushed));
    CHECK(!rules::isAtUnlockedEnd(level, pushed));
}

void testPlayerOnPlateUnlocks()
{
    TEST("playerOnPlateUnlocks");
    const Level level = makeLevel({
        { ".." },
        { "CP" },
    });
    const GameState moved = rules::step(level, rules::initialState(level), MoveDirection::Right);
    CHECK(moved.player == cell(1, 0, 1));
    CHECK(rules::isEndUnlocked(level, moved));
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

    const GameState drowned = rules::step(level, state, MoveDirection::Right);
    CHECK(drowned.player == cell(1, 0, 1));
    CHECK(drowned.playerDead);
    CHECK(!drowned.playerSliding);
    CHECK(!rules::isUnfilledWater(level, drowned, cell(1, 0, 1)));

    // Dead players ignore input; the drowned world is inert.
    CHECK(rules::step(level, drowned, MoveDirection::Left) == drowned);
    CHECK(!rules::hasPendingMotion(level, drowned));
}

void testRockFillsWater()
{
    TEST("rockFillsWater");
    const Level level = makeLevel({
        { "..W." },
        { "CR  " },
    });
    GameState state = rules::initialState(level);

    state = rules::step(level, state, MoveDirection::Right);
    CHECK(state.movables[0].cell == cell(2, 0, 1));
    CHECK(state.movables[0].fallen);
    CHECK(!rules::isUnfilledWater(level, state, cell(2, 0, 1)));

    // The filled water is now safe to walk over.
    state = rules::step(level, state, MoveDirection::Right);
    CHECK(state.player == cell(2, 0, 1));
    CHECK(!state.playerDead);
}

void testLadderClimb()
{
    TEST("ladderClimb");
    const Level level = makeLevel({
        { "..." },
        { ".LC" },
    });
    GameState state = rules::initialState(level);

    state = rules::step(level, state, MoveDirection::Left);
    CHECK(state.player == cell(1, 0, 1)); // onto the ladder

    // Moving toward the attached ground climbs on top of it.
    state = rules::step(level, state, MoveDirection::Left);
    CHECK(state.player == cell(0, 0, 2));
    CHECK(!state.playerDead);
}

void testLadderClimbBlockedByMovable()
{
    TEST("ladderClimbBlockedByMovable");
    // A rock sits on top of the ground block the ladder is attached to.
    const Level level = makeLevel({
        { "..." },
        { ".LC" },
        { "R  " },
    });
    GameState state = rules::initialState(level);
    state = rules::step(level, state, MoveDirection::Left); // onto ladder
    CHECK(state.player == cell(1, 0, 1));

    // The climb destination (0, 0, 2) is occupied by the rock, and the flat
    // target is solid ground, so the step changes nothing.
    CHECK(rules::step(level, state, MoveDirection::Left) == state);
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
    const GameState moved = rules::step(level, rules::initialState(level), MoveDirection::Right);
    CHECK(moved.player == cell(1, 0, 0));
    CHECK(!moved.playerDead);
}

void testConveyorCarriesPlayer()
{
    TEST("conveyorCarriesPlayer");
    const Level level = makeLevel({
        { "...." },
        { "C>> " },
    });
    GameState state = rules::initialState(level);
    state.player = cell(1, 0, 1); // standing on the first belt
    CHECK(rules::hasPendingMotion(level, state));

    // Without input the belt carries the player; direct input overrides it.
    const GameState carried = rules::step(level, state);
    CHECK(carried.player == cell(2, 0, 1));

    const GameState steered = rules::step(level, state, MoveDirection::Left);
    CHECK(steered.player == cell(0, 0, 1));
}


void testFastConveyorRate()
{
    TEST("fastConveyorRate");
    const Level level = makeLevel({
        { "....." },
        { "C>>R " },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(1, 0, 1);

    // At two tiles per step the belt carries the rock across both belt cells
    // in a single step; it stops once carried onto plain ground.
    rules::StepRates rates;
    rates.conveyor = 2;
    state = rules::step(level, state, std::nullopt, rates);
    CHECK(state.movables[0].cell == cell(3, 0, 1));
    CHECK(!rules::hasPendingMotion(level, state));
}

void testFastPlayerRate()
{
    TEST("fastPlayerRate");
    const Level level = makeLevel({
        { "....." },
        { "C    " },
    });
    rules::StepRates rates;
    rates.playerMove = 2;
    const GameState moved = rules::step(level, rules::initialState(level), MoveDirection::Right, rates);
    CHECK(moved.player == cell(2, 0, 1));

    // A fast player shoves a pushable along, one push per micro-step.
    const Level pushLevel = makeLevel({
        { "....." },
        { "CR   " },
    });
    const GameState pushed = rules::step(pushLevel, rules::initialState(pushLevel), MoveDirection::Right, rates);
    CHECK(pushed.player == cell(2, 0, 1));
    CHECK(pushed.movables[0].cell == cell(3, 0, 1));
}

void testFastSlideRate()
{
    TEST("fastSlideRate");
    const Level level = makeLevel({
        { "......" },
        { "CI   #" },
    });
    rules::StepRates rates;
    rates.slide = 2;
    GameState state = rules::initialState(level);

    // The push spends the ice's first tile; its fresh momentum spends the
    // second within the same step (budgets follow the current source).
    state = rules::step(level, state, MoveDirection::Right, rates);
    CHECK(state.movables[0].cell == cell(3, 0, 1));
    CHECK(state.movables[0].sliding == MoveDirection::Right);

    // Two more tiles of slide; the wall is next, so momentum ends.
    state = rules::step(level, state, std::nullopt, rates);
    CHECK(state.movables[0].cell == cell(4, 0, 1));
    CHECK(!state.movables[0].sliding);
    CHECK(!rules::hasPendingMotion(level, state));
}

void testZeroRateFreezesSource()
{
    TEST("zeroRateFreezesSource");
    const Level level = makeLevel({
        { "...." },
        { "C> R " },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(1, 0, 1);

    // Conveyors at zero tiles per step carry nothing, but input still works.
    rules::StepRates rates;
    rates.conveyor = 0;
    const GameState stepped = rules::step(level, state, MoveDirection::Up, rates);
    CHECK(stepped.movables[0].cell == cell(1, 0, 1));
    CHECK(stepped == state); // up is out of bounds here, so nothing changed
}

void testContestedConveyorDestinationBlocksEveryMover()
{
    TEST("contestedConveyorDestinationBlocksEveryMover");
    const Level level = makeLevel({
        { "...", "..." },
        { "> <", "CRR" },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(0, 0, 1);
    state.movables[1].cell = cell(2, 0, 1);

    CHECK(rules::hasPendingMotion(level, state));
    CHECK(rules::step(level, state) == state);

    // Resolution must not depend on movable vector order.
    std::ranges::reverse(state.movables);
    CHECK(rules::step(level, state) == state);
}

void testPlayerAndMovableContestingDestinationBothWait()
{
    TEST("playerAndMovableContestingDestinationBothWait");
    const Level level = makeLevel({
        { "..", "..", ".." },
        { "> ", " ^", "CR" },
    });
    GameState state = rules::initialState(level);
    state.player = cell(0, 0, 1);
    state.movables[0].cell = cell(1, 1, 1);

    const GameState stepped = rules::step(level, state);
    CHECK(stepped.player == state.player);
    CHECK(stepped.movables[0].cell == state.movables[0].cell);
}

void testConveyorChainMovesIntoVacatedCells()
{
    TEST("conveyorChainMovesIntoVacatedCells");
    const Level level = makeLevel({
        { "...", "..." },
        { ">> ", "CRR" },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(0, 0, 1);
    state.movables[1].cell = cell(1, 0, 1);

    const GameState stepped = rules::step(level, state);
    CHECK(stepped.movables[0].cell == cell(1, 0, 1));
    CHECK(stepped.movables[1].cell == cell(2, 0, 1));
}

void testHeadOnSlidesStopWithoutOverlap()
{
    TEST("headOnSlidesStopWithoutOverlap");
    const Level level = makeLevel({
        { "...", "..." },
        { "   ", "CRR" },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(0, 0, 1);
    state.movables[0].sliding = MoveDirection::Right;
    state.movables[1].cell = cell(2, 0, 1);
    state.movables[1].sliding = MoveDirection::Left;

    const GameState stepped = rules::step(level, state);
    CHECK(stepped.movables[0].cell == cell(0, 0, 1));
    CHECK(stepped.movables[1].cell == cell(2, 0, 1));
    CHECK(!stepped.movables[0].sliding);
    CHECK(!stepped.movables[1].sliding);
    CHECK(!rules::hasPendingMotion(level, stepped));
}

void testEveryPressurePlateMustHaveLiveOccupant()
{
    TEST("everyPressurePlateMustHaveLiveOccupant");
    const Level level = makeLevel({
        { "......" },
        { "CPPR E" },
    });
    GameState state = rules::initialState(level);
    CHECK(!rules::isEndUnlocked(level, state));

    state.player = cell(2, 0, 1);
    state.movables[0].cell = cell(1, 0, 1);
    CHECK(rules::isEndUnlocked(level, state));

    state.movables[0].fallen = true;
    CHECK(!rules::isEndUnlocked(level, state));

    const Level noPlates = makeLevel({
        { ".." },
        { "CE" },
    });
    CHECK(rules::isEndUnlocked(noPlates, rules::initialState(noPlates)));
}

} // namespace

int main()
{
    testInitialState();
    testStepMovesPlayer();
    testStepIsPure();
    testPushRock();
    testPushBlocked();
    testIceSlidesOneTilePerStep();
    testPlayerMovesWhileIceSlides();
    testPlayerMovesWhileConveyorCarriesRock();
    testConveyorMovesRockEachStep();
    testConveyorBlocked();
    testConveyorRockBlockedByPlayer();
    testConveyorRockIntoWater();
    testIceIntoWater();
    testPlayerSlidesOnFallenIce();
    testSlideMomentumOverridesInput();
    testPressurePlateUnlocksEnd();
    testPlayerOnPlateUnlocks();
    testPlayerDrownsInWater();
    testRockFillsWater();
    testLadderClimb();
    testLadderClimbBlockedByMovable();
    testFallToLowerLayer();
    testConveyorCarriesPlayer();
    testFastConveyorRate();
    testFastPlayerRate();
    testFastSlideRate();
    testZeroRateFreezesSource();
    testContestedConveyorDestinationBlocksEveryMover();
    testPlayerAndMovableContestingDestinationBothWait();
    testConveyorChainMovesIntoVacatedCells();
    testHeadOnSlidesStopWithoutOverlap();
    testEveryPressurePlateMustHaveLiveOccupant();

    if (failures == 0) {
        std::cout << "All " << checks << " checks passed.\n";
        return 0;
    }

    std::cerr << failures << " of " << checks << " checks failed.\n";
    return 1;
}
