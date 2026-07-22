#include "engine/LevelCatalog.hpp"

#include <iostream>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* label)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

void testValidSavedLocationIsPreserved()
{
    const std::vector<int> screenCounts { 1, 3, 2 };
    const sokoban::LevelLocation saved { .level = 1, .screen = 2 };

    check(sokoban::levelLocationExists(screenCounts, saved),
        "saved location exists in populated catalog");
    check(sokoban::resolveSavedLevelLocation(screenCounts, saved) == saved,
        "valid saved level and screen are preserved");
}

void testCatalogBoundaries()
{
    const std::vector<int> screenCounts { 1, 3 };

    check(sokoban::levelLocationExists(screenCounts, { 0, 0 }),
        "first screen exists");
    check(sokoban::levelLocationExists(screenCounts, { 1, 2 }),
        "last screen exists");
    check(!sokoban::levelLocationExists(screenCounts, { -1, 0 }),
        "negative level rejected");
    check(!sokoban::levelLocationExists(screenCounts, { 0, -1 }),
        "negative screen rejected");
    check(!sokoban::levelLocationExists(screenCounts, { 2, 0 }),
        "level past catalog rejected");
    check(!sokoban::levelLocationExists(screenCounts, { 1, 3 }),
        "screen past level rejected");
}

void testInvalidSavedLocationFallsBackToStart()
{
    const std::vector<int> screenCounts { 1, 2 };

    check(sokoban::resolveSavedLevelLocation(screenCounts, { 4, 8 }) ==
            sokoban::LevelLocation {},
        "invalid saved location falls back to first screen");
    check(sokoban::resolveSavedLevelLocation({}, { 1, 1 }) ==
            sokoban::LevelLocation {},
        "empty catalog falls back to first screen");
}

} // namespace

int main()
{
    testValidSavedLocationIsPreserved();
    testCatalogBoundaries();
    testInvalidSavedLocationFallsBackToStart();

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << checks << " checks passed\n";
    return 0;
}
