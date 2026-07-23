#include "engine\GameplayLoop.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace sokoban;

int failures = 0;
int checks = 0;
const char* currentTest = "";

void checkImpl(bool value, const char* expression, int line)
{
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL [" << currentTest << "] line " << line
                  << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)
#define TEST(name) currentTest = name

Level makeLevel(const std::vector<std::vector<std::string>>& layers)
{
    std::vector<std::string> lines;
    for (std::size_t layer = 0; layer < layers.size(); ++layer) {
        lines.push_back("@layer " + std::to_string(layer));
        lines.insert(lines.end(), layers[layer].begin(), layers[layer].end());
        lines.emplace_back();
    }
    return Level::loadFromLines(lines, "gameplay loop test");
}

void testOpposingDirectionsAreNeutral()
{
    TEST("opposingDirectionsAreNeutral");
    GameplayLoop::InputFrame input {
        .up = { .pressed = true, .down = true },
        .down = { .pressed = true, .down = true },
        .left = { .pressed = true, .down = true },
        .right = { .pressed = true, .down = true },
    };
    CHECK(!GameplayLoop::pressedVertical(input).has_value());
    CHECK(!GameplayLoop::pressedHorizontal(input).has_value());
    CHECK(!GameplayLoop::heldVertical(input).has_value());
    CHECK(!GameplayLoop::heldHorizontal(input).has_value());
}

void testMoveAdvancesSessionAndPresentation()
{
    TEST("moveAdvancesSessionAndPresentation");
    const Level level = makeLevel({ { "..." }, { "C  " } });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());

    const GameplayLoop::UpdateResult result = GameplayLoop::update(
        level,
        session,
        presentation,
        { .right = { .pressed = true, .down = true } },
        0.1f,
        false);
    CHECK(result.stateCommitted);
    CHECK(!result.screenSolved);
    CHECK(session.state().player == (GridPosition3 { 1, 0, 1 }));
    CHECK(session.playerMoveCount() == 1);
}

void testSolvedScreenAndDraftOutcomesDiffer()
{
    TEST("solvedScreenAndDraftOutcomesDiffer");
    const Level level = makeLevel({ { ".." }, { "CE" } });

    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    GameplayLoop::UpdateResult result = GameplayLoop::update(
        level,
        session,
        presentation,
        { .right = { .pressed = true, .down = true } },
        0.1f,
        false);
    CHECK(result.screenSolved);
    CHECK(!result.draftSolved);
    CHECK(!result.stateCommitted);

    session.reset(level);
    presentation.resetEntities(session.state());
    result = GameplayLoop::update(
        level,
        session,
        presentation,
        { .right = { .pressed = true, .down = true } },
        0.1f,
        true);
    CHECK(!result.screenSolved);
    CHECK(result.draftSolved);
}

} // namespace

int main()
{
    testOpposingDirectionsAreNeutral();
    testMoveAdvancesSessionAndPresentation();
    testSolvedScreenAndDraftOutcomesDiffer();

    if (failures == 0) {
        std::cout << "GameplayLoopTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "GameplayLoopTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
