#include "TestHarness.hpp"

#include "engine/LevelTransition.hpp"

#include <cmath>
#include <iostream>

namespace {

void testClosesSwapsAndOpens()
{
    sokoban::LevelTransition transition;
    CHECK(!transition.active());
    CHECK(transition.amount() == 0.0f);
    CHECK(transition.start());
    CHECK(!transition.start());

    auto result = transition.update(
        sokoban::LevelTransition::closingDurationSeconds * 0.5f);
    CHECK(!result.midpointReached);
    CHECK(std::abs(transition.amount() - 0.5f) < 0.0001f);

    result = transition.update(
        sokoban::LevelTransition::closingDurationSeconds);
    CHECK(result.midpointReached);
    CHECK(!result.finished);
    CHECK(transition.active());
    CHECK(transition.amount() == 1.0f);

    result = transition.update(
        sokoban::LevelTransition::openingDurationSeconds * 0.5f);
    CHECK(!result.midpointReached);
    CHECK(!result.finished);
    CHECK(std::abs(transition.amount() - 0.5f) < 0.0001f);

    result = transition.update(
        sokoban::LevelTransition::openingDurationSeconds * 0.5f);
    CHECK(result.finished);
    CHECK(!transition.active());
    CHECK(transition.amount() == 0.0f);
}

void testNegativeTimeAndIdleUpdatesAreIgnored()
{
    sokoban::LevelTransition transition;
    CHECK(!transition.update(100.0f).finished);
    CHECK(transition.start());
    CHECK(!transition.update(-1.0f).midpointReached);
    CHECK(transition.amount() == 0.0f);
}

} // namespace

int main()
{
    testClosesSwapsAndOpens();
    testNegativeTimeAndIdleUpdatesAreIgnored();

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << checks << " checks passed\n";
    return 0;
}
