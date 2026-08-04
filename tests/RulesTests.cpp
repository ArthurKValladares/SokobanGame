// Headless tests for the step-based gameplay rules engine. No SDL/Vulkan
// dependencies: this file compiles against Level, TileTypes, and Rules only.

#include "engine/Level.hpp"
#include "engine/Rules.hpp"

#include <algorithm>
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

    CHECK(state.players[0].cell == cell(0, 0, 1));
    CHECK(!state.players[0].dead);
    CHECK(!state.players[0].sliding);
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
    CHECK(left.players[0].cell == cell(0, 0, 1));
    CHECK(!left.players[0].dead);

    // Blocked by the wall / by the level edge: the step changes nothing.
    CHECK(rules::step(level, state, MoveDirection::Right) == state);
    CHECK(rules::step(level, state, MoveDirection::Up) == state);

    // No input and nothing pending: nothing happens.
    CHECK(rules::step(level, state) == state);
}

void testDecorativeTileDoesNotBlockMovement()
{
    TEST("decorativeTileDoesNotBlockMovement");
    const Level level = makeLevel({
        { "..." },
        { "CD " },
    });
    const GameState state = rules::initialState(level);

    const GameState moved = rules::step(level, state, MoveDirection::Right);
    CHECK(moved.players[0].cell == cell(1, 0, 1));
    CHECK(level.tileAt(1, 0, 1) == TileType::Decorative);
    CHECK(moved.movables.empty());
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
    CHECK(pushed.players[0].cell == cell(1, 0, 1));
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
    CHECK(state.players[0].cell == cell(1, 0, 1));
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
    CHECK(state.players[0].cell == cell(1, 0, 1));
    CHECK(state.movables[0].cell == cell(2, 0, 1));
    CHECK(state.movables[0].sliding == MoveDirection::Right);

    // While the ice keeps sliding, the player walks somewhere else in the
    // very same step.
    state = rules::step(level, state, MoveDirection::Down);
    CHECK(state.players[0].cell == cell(1, 1, 1));
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
    CHECK(state.players[0].cell == cell(1, 0, 1));
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
    CHECK(state.players[0].cell == cell(0, 0, 1)); // player never moved
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
    CHECK(state.players[0].cell == cell(2, 0, 1));

    // Stepping onto the ice-filled water gives the player slide momentum.
    state = rules::step(level, state, MoveDirection::Right);
    CHECK(state.players[0].cell == cell(3, 0, 1));
    CHECK(state.players[0].sliding == MoveDirection::Right);
    CHECK(rules::hasPendingMotion(level, state));

    // Momentum carries the player off the ice, then ends on normal ground.
    state = rules::step(level, state);
    CHECK(state.players[0].cell == cell(4, 0, 1));
    CHECK(!state.players[0].sliding);
    CHECK(!state.players[0].dead);
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
    CHECK(state.players[0].sliding == MoveDirection::Right);

    // Input cannot steer a sliding player: the slide continues instead.
    state = rules::step(level, state, MoveDirection::Down);
    CHECK(state.players[0].cell == cell(4, 0, 1));
    CHECK(state.players[0].cell.y == 0);
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
    CHECK(moved.players[0].cell == cell(1, 0, 1));
    CHECK(rules::isEndUnlocked(level, moved));
}

void testPlayerDrownsInWater()
{
    TEST("playerDrownsInWater");
    const Level level = Level::loadFromLayers(
        {
            { ". " },
            { "C " },
        },
        "water-layer drowning",
        0U);
    const GameState state = rules::initialState(level);
    CHECK(rules::isUnfilledWater(level, state, cell(1, 0, 1)));

    const GameState drowned = rules::step(level, state, MoveDirection::Right);
    CHECK(drowned.players[0].cell == cell(1, 0, 1));
    CHECK(drowned.players[0].dead);
    CHECK(!drowned.players[0].sliding);
    CHECK(rules::isUnfilledWater(level, drowned, cell(1, 0, 1)));

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
    CHECK(state.players[0].cell == cell(2, 0, 1));
    CHECK(!state.players[0].dead);
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
    CHECK(state.players[0].cell == cell(1, 0, 1)); // onto the ladder

    // Moving toward the attached ground climbs on top of it.
    state = rules::step(level, state, MoveDirection::Left);
    CHECK(state.players[0].cell == cell(0, 0, 2));
    CHECK(!state.players[0].dead);
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
    CHECK(state.players[0].cell == cell(1, 0, 1));

    // The climb destination (0, 0, 2) is occupied by the rock, and the flat
    // target is solid ground, so the step changes nothing.
    CHECK(rules::step(level, state, MoveDirection::Left) == state);
}

void testUnsupportedMovesAreBlocked()
{
    TEST("unsupportedMovesAreBlocked");
    // Walking into a column with nothing that can hold an entity is refused
    // outright; entities never rest on air.
    const Level level = makeLevel({
        { ".  " },
        { "C  " },
    });
    const GameState blocked = rules::step(level, rules::initialState(level), MoveDirection::Right);
    CHECK(blocked.players[0].cell == cell(0, 0, 1));
    CHECK(!blocked.players[0].dead);
}

void testSupportedDropIsStillAllowed()
{
    TEST("supportedDropIsStillAllowed");
    // Stepping off a raised platform is fine when the landing column has
    // real support further down.
    const Level level = makeLevel({
        { "..." },
        { ".. " },
        { "C  " },
    });
    GameState state = rules::step(level, rules::initialState(level), MoveDirection::Right);
    CHECK(state.players[0].cell == cell(1, 0, 2));
    state = rules::step(level, state, MoveDirection::Right);
    CHECK(state.players[0].cell == cell(2, 0, 1));
    CHECK(!state.players[0].dead);
}

void testPushIntoVoidIsBlocked()
{
    TEST("pushIntoVoidIsBlocked");
    const Level level = makeLevel({
        { "..  " },
        { "CR  " },
    });
    const GameState after = rules::step(level, rules::initialState(level), MoveDirection::Right);
    CHECK(after.players[0].cell == cell(0, 0, 1));
    CHECK(after.movables[0].cell == cell(1, 0, 1));
    CHECK(!after.movables[0].fallen);
}

void testConveyorHoldsRiderAtVoidEdge()
{
    TEST("conveyorHoldsRiderAtVoidEdge");
    const Level level = makeLevel({
        { "...  " },
        { "C>R  " },
    });
    const GameState after = rules::step(level, rules::initialState(level), std::nullopt);
    CHECK(after.movables[0].cell == cell(2, 0, 1));
    CHECK(!after.movables[0].fallen);
}

void testConveyorCarriesPlayer()
{
    TEST("conveyorCarriesPlayer");
    const Level level = makeLevel({
        { "...." },
        { "C>> " },
    });
    GameState state = rules::initialState(level);
    state.players[0].cell = cell(1, 0, 1); // standing on the first belt
    CHECK(rules::hasPendingMotion(level, state));

    // Without input the belt carries the player; direct input overrides it.
    const GameState carried = rules::step(level, state);
    CHECK(carried.players[0].cell == cell(2, 0, 1));

    const GameState steered = rules::step(level, state, MoveDirection::Left);
    CHECK(steered.players[0].cell == cell(0, 0, 1));
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
    CHECK(moved.players[0].cell == cell(2, 0, 1));

    // A fast player shoves a pushable along, one push per micro-step.
    const Level pushLevel = makeLevel({
        { "....." },
        { "CR   " },
    });
    const GameState pushed = rules::step(pushLevel, rules::initialState(pushLevel), MoveDirection::Right, rates);
    CHECK(pushed.players[0].cell == cell(2, 0, 1));
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
    state.players[0].cell = cell(0, 0, 1);
    state.movables[0].cell = cell(1, 1, 1);

    const GameState stepped = rules::step(level, state);
    CHECK(stepped.players[0].cell == state.players[0].cell);
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

    state.players[0].cell = cell(2, 0, 1);
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

void testEveryMirrorOrientationReflectsBothWays()
{
    TEST("everyMirrorOrientationReflectsBothWays");
    struct Case {
        char mirror;
        GridPosition3 first;
        GridPosition3 firstExpected;
        GridPosition3 second;
        GridPosition3 secondExpected;
    };
    const std::vector<Case> cases {
        { '1', cell(2, 0, 1), cell(0, 2, 1), cell(0, 2, 1), cell(2, 0, 1) },
        { '2', cell(2, 0, 1), cell(4, 2, 1), cell(4, 2, 1), cell(2, 0, 1) },
        { '3', cell(2, 4, 1), cell(0, 2, 1), cell(0, 2, 1), cell(2, 4, 1) },
        { '4', cell(2, 4, 1), cell(4, 2, 1), cell(4, 2, 1), cell(2, 4, 1) },
    };

    for (const Case& test : cases) {
        std::vector<std::string> mirrorLayer(5, "     ");
        mirrorLayer[2][2] = test.mirror;
        mirrorLayer[static_cast<std::size_t>(test.first.y)]
            [static_cast<std::size_t>(test.first.x)] = 'C';
        const Level level = makeLevel({
            { ".....", ".....", ".....", ".....", "....." },
            mirrorLayer,
        });
        GameState state = rules::initialState(level);
        const GameState original = state;
        const std::optional<GameState> first = rules::activateMirrors(level, state);
        CHECK(first.has_value());
        CHECK(first && first->players[0].cell == test.firstExpected);
        CHECK(state == original);

        state.players[0].cell = test.second;
        const std::optional<GameState> second = rules::activateMirrors(level, state);
        CHECK(second.has_value());
        CHECK(second && second->players[0].cell == test.secondExpected);
    }
}

void testMirrorReflectsMovablesAndStopsAtNearestEntity()
{
    TEST("mirrorReflectsMovablesAndStopsAtNearestEntity");
    const Level level = makeLevel({
        { ".....", ".....", ".....", ".....", "....." },
        { "C    ", "     ", "  3  ", "  R  ", "  I  " },
    });
    const GameState state = rules::initialState(level);
    const std::optional<GameState> after = rules::activateMirrors(level, state);

    CHECK(after.has_value());
    CHECK(after && after->players[0].cell == state.players[0].cell);
    CHECK(after && after->movables[0].type == TileType::Rock);
    CHECK(after && after->movables[0].cell == cell(1, 2, 1));
    // The nearer rock occludes the ice on the same input ray.
    CHECK(after && after->movables[1].cell == cell(2, 4, 1));
}

void testMirrorChainsWithoutReusingAMirror()
{
    TEST("mirrorChainsWithoutReusingAMirror");
    const Level level = makeLevel({
        { ".....", ".....", ".....", ".....", "....." },
        { "  3  ", "     ", "    3", "     ", "    C" },
    });
    const GameState state = rules::initialState(level);
    const std::optional<rules::MirrorActivationPreview> preview =
        rules::previewMirrorActivation(level, state);
    CHECK(preview.has_value());
    CHECK(preview && preview->entities.size() == 1);
    CHECK(preview && preview->entities[0].player);
    CHECK(preview && preview->entities[0].start == cell(4, 4, 1));
    CHECK(preview && preview->entities[0].destination == cell(0, 0, 1));
    CHECK(preview && preview->entities[0].beamSegments.size() == 4);
    if (preview && preview->entities[0].beamSegments.size() == 4) {
        CHECK(preview->entities[0].beamSegments[0] ==
            (rules::MirrorBeamSegment { cell(4, 4, 1), cell(4, 2, 1) }));
        CHECK(preview->entities[0].beamSegments[1] ==
            (rules::MirrorBeamSegment { cell(4, 2, 1), cell(2, 2, 1) }));
        CHECK(preview->entities[0].beamSegments[2] ==
            (rules::MirrorBeamSegment { cell(2, 2, 1), cell(2, 0, 1) }));
        CHECK(preview->entities[0].beamSegments[3] ==
            (rules::MirrorBeamSegment { cell(2, 0, 1), cell(0, 0, 1) }));
    }
    const std::optional<GameState> after = rules::activateMirrors(level, state);
    CHECK(after.has_value());
    CHECK(after && after->players[0].cell == cell(0, 0, 1));
    CHECK(preview && after && preview->after == *after);
}

void testInvalidMirrorOutputRejectsWholeActivation()
{
    TEST("invalidMirrorOutputRejectsWholeActivation");
    const Level level = makeLevel({
        { ".....", ".....", ".....", ".....", "....." },
        { "     ", "     ", " #3  ", "     ", "  C  " },
    });
    const GameState state = rules::initialState(level);
    CHECK(!rules::activateMirrors(level, state));
    CHECK(state.players[0].cell == cell(2, 4, 1));
}

void testMirrorCanTeleportPlayerIntoWater()
{
    TEST("mirrorCanTeleportPlayerIntoWater");
    const Level level = makeLevel({
        { ".....", ".....", "W....", ".....", "....." },
        { "     ", "     ", "  3  ", "     ", "  C  " },
    });
    const std::optional<GameState> after =
        rules::activateMirrors(level, rules::initialState(level));
    CHECK(after.has_value());
    CHECK(after && after->players[0].cell == cell(0, 2, 1));
    CHECK(after && after->players[0].dead);

    const std::optional<rules::MirrorActivationPreview> preview =
        rules::previewMirrorActivation(level, rules::initialState(level));
    CHECK(preview && preview->entities[0].fallen);
}

void testMirrorNoVisibilityIsNoOp()
{
    TEST("mirrorNoVisibilityIsNoOp");
    const Level level = makeLevel({
        { ".....", ".....", ".....", ".....", "....." },
        { "C    ", "     ", "  3  ", "     ", "     " },
    });
    CHECK(!rules::activateMirrors(level, rules::initialState(level)));
}

void testEquidistantMirrorsDuplicatePlayers()
{
    TEST("equidistantMirrorsDuplicatePlayers");
    const Level level = makeLevel({
        { ".....", ".....", ".....", ".....", "....." },
        { "  3  ", "     ", "  C  ", "     ", "  2  " },
    });
    const GameState state = rules::initialState(level);
    const std::optional<rules::MirrorActivationPreview> preview =
        rules::previewMirrorActivation(level, state);

    CHECK(preview.has_value());
    CHECK(preview && preview->after.players.size() == 2);
    CHECK(preview && preview->after.players[0].cell == cell(0, 0, 1));
    CHECK(preview && preview->after.players.size() == 2);
    CHECK(preview && preview->after.players[1].cell == cell(4, 4, 1));
    CHECK(preview && preview->entities.size() == 2);
    CHECK(preview && preview->entities[0].player);
    CHECK(preview && preview->entities[0].playerIndex == 0);
    CHECK(preview && preview->entities[0].reflectionIndex == 0);
    CHECK(preview && preview->entities[1].reflectionIndex == 1);
}

void testPlayerCopiesShareMovementAndCanDuplicateAgain()
{
    TEST("playerCopiesShareMovementAndCanDuplicateAgain");
    const Level openLevel = makeLevel({
        { ".....", ".....", "....." },
        { "C    ", "     ", "     " },
    });
    GameState state = rules::initialState(openLevel);
    state.players.push_back({ .cell = cell(0, 2, 1) });
    const GameState moved = rules::step(
        openLevel, state, MoveDirection::Right);
    CHECK(moved.players[0].cell == cell(1, 0, 1));
    CHECK(moved.players[1].cell == cell(1, 2, 1));

    const Level mirrorLevel = makeLevel({
        { ".....", ".....", ".....", ".....", "....." },
        { "  3  ", "     ", "  C  ", "     ", "  2  " },
    });
    GameState duplicated = rules::initialState(mirrorLevel);
    duplicated.players[0].cell = cell(4, 2, 1);
    duplicated.players.push_back({ .cell = cell(2, 2, 1) });
    const std::optional<GameState> duplicatedAgain =
        rules::activateMirrors(mirrorLevel, duplicated);
    CHECK(duplicatedAgain.has_value());
    CHECK(duplicatedAgain && duplicatedAgain->players.size() == 3);
}

void testEveryPlayerMustReachAnActiveEnd()
{
    TEST("everyPlayerMustReachAnActiveEnd");
    const Level level = makeLevel({
        { "...." },
        { "CE E" },
    });
    GameState state = rules::initialState(level);
    state.players[0].cell = cell(1, 0, 1);
    state.players.push_back({ .cell = cell(3, 0, 1) });
    CHECK(rules::isAtUnlockedEnd(level, state));

    state.players[0].cell = cell(2, 0, 1);
    CHECK(!rules::isAtUnlockedEnd(level, state));
    state.players[1].cell = cell(3, 0, 1);
    state.players[1].dead = true;
    CHECK(!rules::isAtUnlockedEnd(level, state));
}

void testEnemySpawnsOutsideStaticGridAndKillsAdjacentPlayer()
{
    TEST("enemySpawnsOutsideStaticGridAndKillsAdjacentPlayer");
    const Level level = makeLevel({
        { "...." },
        { "C N " },
    });
    const GameState state = rules::initialState(level);
    CHECK(state.enemies.size() == 1);
    CHECK(state.enemies[0].cell == cell(2, 0, 1));
    CHECK(level.authoredTileAt(2, 0, 1) == TileType::Air);

    const GameState attacked = rules::step(
        level, state, MoveDirection::Right);
    CHECK(attacked.players[0].cell == cell(1, 0, 1));
    CHECK(attacked.players[0].dead);
    CHECK(!attacked.players[0].drowned);
}

void testEnemyDoesNotAttackDiagonallyAndBlocksDirectMovement()
{
    TEST("enemyDoesNotAttackDiagonallyAndBlocksDirectMovement");
    const Level diagonal = makeLevel({
        { "...", "...", "..." },
        { " N ", "C  ", "   " },
    });
    const GameState diagonalState = rules::step(
        diagonal, rules::initialState(diagonal), MoveDirection::Down);
    CHECK(diagonalState.players[0].cell == cell(0, 2, 1));
    CHECK(!diagonalState.players[0].dead);

    const Level blocked = makeLevel({
        { "..." },
        { "CN " },
    });
    const GameState start = rules::initialState(blocked);
    CHECK(rules::step(blocked, start, MoveDirection::Right) == start);
}

void testMovingBlockPushesEnemy()
{
    TEST("movingBlockPushesEnemy");
    const Level level = makeLevel({
        { "....." },
        { "CRN  " },
    });
    const GameState pushed = rules::step(
        level, rules::initialState(level), MoveDirection::Right);
    CHECK(pushed.players[0].cell == cell(1, 0, 1));
    CHECK(pushed.movables[0].cell == cell(2, 0, 1));
    CHECK(pushed.enemies[0].cell == cell(3, 0, 1));
    CHECK(!pushed.enemies[0].fallen);
    CHECK(!pushed.players[0].dead);
}

// A scope names the entities an action is allowed to move. These pin the two
// halves of that: in-scope entities behave exactly as they always did, and
// out-of-scope entities keep every passive role while never being written.

void testEmptyScopeIsTheWholeWorldStep()
{
    TEST("emptyScopeIsTheWholeWorldStep");
    const Level level = makeLevel({
        { "....." },
        { "C> R " },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(1, 0, 1);

    // Every save in existence is validated by replaying it through the
    // whole-world form, so this equivalence is not a nicety.
    CHECK(rules::scopedStep(
              level, state, MoveDirection::Right, {}, rules::StepScope {}) ==
        rules::step(level, state, MoveDirection::Right));
}

void testScopeLeavesAmbientMotionAlone()
{
    TEST("scopeLeavesAmbientMotionAlone");
    // The rock rides a belt on the row below, well clear of the player, so
    // nothing here is a push.
    const Level level = makeLevel({
        { ".....", "....." },
        { "C   R", " >   " },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(1, 1, 1); // onto the belt
    CHECK(rules::hasPendingMotion(level, state));

    // Only the player acts. The belt is running under the rock, but this
    // action is not the one carrying it, so the rock must be untouched - that
    // is what stops a player's plan from re-planning a ride already in flight.
    const rules::StepScope playerOnly { .actors = { state.players[0].id } };
    const GameState after = rules::scopedStep(
        level, state, MoveDirection::Right, {}, playerOnly);

    CHECK(after.players[0].cell == cell(1, 0, 1));
    CHECK(after.movables[0] == state.movables[0]);

    // The whole-world step is what carries it, and still does.
    const GameState whole = rules::step(level, state, MoveDirection::Right);
    CHECK(whole.movables[0].cell == cell(2, 1, 1));
}

void testOutOfScopeEntitiesStillBlock()
{
    TEST("outOfScopeEntitiesStillBlock");
    const Level level = makeLevel({
        { "...." },
        { "CR# " },
    });
    GameState state = rules::initialState(level);

    // Scenery, not absence: the rock cannot be pushed into the wall, so the
    // player does not move either.
    const rules::StepScope playerOnly { .actors = { state.players[0].id } };
    const GameState after = rules::scopedStep(
        level, state, MoveDirection::Right, {}, playerOnly);

    CHECK(after == state);
}

void testPushingBringsTheMovableIntoTheClosure()
{
    TEST("pushingBringsTheMovableIntoTheClosure");
    const Level level = makeLevel({
        { "....." },
        { "CI  #" },
    });
    GameState state = rules::initialState(level);

    // The ice was never named, but pushing it is what makes it part of this
    // action - including the momentum it leaves the step carrying.
    const rules::StepScope playerOnly { .actors = { state.players[0].id } };
    const GameState after = rules::scopedStep(
        level, state, MoveDirection::Right, {}, playerOnly);

    CHECK(after.players[0].cell == cell(1, 0, 1));
    CHECK(after.movables[0].cell == cell(2, 0, 1));
    CHECK(after.movables[0].sliding == MoveDirection::Right);
}

void testScopedSlideDoesNotMoveThePlayer()
{
    TEST("scopedSlideDoesNotMoveThePlayer");
    const Level level = makeLevel({
        { "....." },
        { "CI  #" },
    });
    GameState state = rules::step(level, rules::initialState(level),
        MoveDirection::Right);

    // The slide continues on its own. Input is present but the player is not
    // in scope, so it is not this action's to spend - the responsiveness the
    // whole design is for depends on these being separable.
    const rules::StepScope iceOnly { .actors = { state.movables[0].id } };
    const GameState after = rules::scopedStep(
        level, state, MoveDirection::Right, {}, iceOnly);

    CHECK(after.movables[0].cell == cell(3, 0, 1));
    CHECK(after.players[0] == state.players[0]);
}

void testScopedActionDoesNotKillABystander()
{
    TEST("scopedActionDoesNotKillABystander");
    const Level level = makeLevel({
        { "......" },
        { "C   N " },
    });
    GameState state = rules::initialState(level);
    GameState::Player bystander;
    bystander.id = 99;
    bystander.cell = cell(3, 0, 1); // already standing next to the enemy
    state.players.push_back(bystander);

    // Adjacency is a standing fact about the board, so an unscoped sweep would
    // have any action anywhere kill this player for where they already were.
    const rules::StepScope firstOnly { .actors = { state.players[0].id } };
    const GameState scoped = rules::scopedStep(
        level, state, MoveDirection::Right, {}, firstOnly);
    CHECK(scoped.players[0].cell == cell(1, 0, 1));
    CHECK(!scoped.players[1].dead);

    // The whole-world step still resolves attacks over everyone, unchanged.
    const GameState whole = rules::step(level, state, MoveDirection::Right);
    CHECK(whole.players[1].dead);
}

void testShovingAnEnemyPullsItsVictimIntoTheClosure()
{
    TEST("shovingAnEnemyPullsItsVictimIntoTheClosure");
    const Level level = makeLevel({
        { "......" },
        { "CRN   " },
    });
    GameState state = rules::initialState(level);
    GameState::Player victim;
    victim.id = 99;
    victim.cell = cell(4, 0, 1); // two cells away, so safe until the shove
    state.players.push_back(victim);

    // Seeded with one player, the action ends up writing three entities: it
    // pushed the rock, the rock shoved the enemy, and the enemy killed someone
    // who was never named. That growth is the causal closure.
    const rules::StepScope firstOnly { .actors = { state.players[0].id } };
    const GameState after = rules::scopedStep(
        level, state, MoveDirection::Right, {}, firstOnly);

    CHECK(after.movables[0].cell == cell(2, 0, 1));
    CHECK(after.enemies[0].cell == cell(3, 0, 1));
    CHECK(after.players[1].dead);
}

} // namespace

int main()
{
    testInitialState();
    testStepMovesPlayer();
    testDecorativeTileDoesNotBlockMovement();
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
    testUnsupportedMovesAreBlocked();
    testSupportedDropIsStillAllowed();
    testPushIntoVoidIsBlocked();
    testConveyorHoldsRiderAtVoidEdge();
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
    testEveryMirrorOrientationReflectsBothWays();
    testMirrorReflectsMovablesAndStopsAtNearestEntity();
    testMirrorChainsWithoutReusingAMirror();
    testInvalidMirrorOutputRejectsWholeActivation();
    testMirrorCanTeleportPlayerIntoWater();
    testMirrorNoVisibilityIsNoOp();
    testEquidistantMirrorsDuplicatePlayers();
    testPlayerCopiesShareMovementAndCanDuplicateAgain();
    testEveryPlayerMustReachAnActiveEnd();
    testEnemySpawnsOutsideStaticGridAndKillsAdjacentPlayer();
    testEnemyDoesNotAttackDiagonallyAndBlocksDirectMovement();
    testMovingBlockPushesEnemy();
    testEmptyScopeIsTheWholeWorldStep();
    testScopeLeavesAmbientMotionAlone();
    testOutOfScopeEntitiesStillBlock();
    testPushingBringsTheMovableIntoTheClosure();
    testScopedSlideDoesNotMoveThePlayer();
    testScopedActionDoesNotKillABystander();
    testShovingAnEnemyPullsItsVictimIntoTheClosure();

    if (failures == 0) {
        std::cout << "All " << checks << " checks passed.\n";
        return 0;
    }

    std::cerr << failures << " of " << checks << " checks failed.\n";
    return 1;
}
