#include "TestHarness.hpp"
#include "ScopedTestDirectory.hpp"

#include "engine/LevelCatalog.hpp"

#include <filesystem>
#include <iostream>
#include <vector>

namespace {

void testValidSavedLocationIsPreserved()
{
    const std::vector<int> screenCounts { 1, 3, 2 };
    const sokoban::LevelLocation saved { .level = 1, .screen = 2 };

    CHECK_MESSAGE(sokoban::levelLocationExists(screenCounts, saved),
        "saved location exists in populated catalog");
    CHECK_MESSAGE(sokoban::resolveSavedLevelLocation(screenCounts, saved) == saved,
        "valid saved level and screen are preserved");
}

void testCatalogBoundaries()
{
    const std::vector<int> screenCounts { 1, 3 };

    CHECK_MESSAGE(sokoban::levelLocationExists(screenCounts, { 0, 0 }),
        "first screen exists");
    CHECK_MESSAGE(sokoban::levelLocationExists(screenCounts, { 1, 2 }),
        "last screen exists");
    CHECK_MESSAGE(!sokoban::levelLocationExists(screenCounts, { -1, 0 }),
        "negative level rejected");
    CHECK_MESSAGE(!sokoban::levelLocationExists(screenCounts, { 0, -1 }),
        "negative screen rejected");
    CHECK_MESSAGE(!sokoban::levelLocationExists(screenCounts, { 2, 0 }),
        "level past catalog rejected");
    CHECK_MESSAGE(!sokoban::levelLocationExists(screenCounts, { 1, 3 }),
        "screen past level rejected");
}

void testInvalidSavedLocationFallsBackToStart()
{
    const std::vector<int> screenCounts { 1, 2 };

    CHECK_MESSAGE(sokoban::resolveSavedLevelLocation(screenCounts, { 4, 8 }) ==
            sokoban::LevelLocation {},
        "invalid saved location falls back to first screen");
    CHECK_MESSAGE(sokoban::resolveSavedLevelLocation({}, { 1, 1 }) ==
            sokoban::LevelLocation {},
        "empty catalog falls back to first screen");
}

void testPuzzlePathConvention()
{
    CHECK_MESSAGE(sokoban::levelIndexFromDirectoryName("level0") == 0,
        "zero level index parses");
    CHECK_MESSAGE(sokoban::levelIndexFromDirectoryName("level0042") == 42,
        "numeric level spelling parses");
    CHECK_MESSAGE(!sokoban::levelIndexFromDirectoryName("level").has_value(),
        "level name requires an index");
    CHECK_MESSAGE(!sokoban::levelIndexFromDirectoryName("level-1").has_value(),
        "negative level index is rejected");
    CHECK_MESSAGE(!sokoban::levelIndexFromDirectoryName("Level1").has_value(),
        "level prefix is case-sensitive");

    CHECK_MESSAGE(sokoban::screenIndexFromFilename("screen3.scr") == 3,
        "screen filename parses");
    CHECK_MESSAGE(!sokoban::screenIndexFromFilename("screen3.txt").has_value(),
        "screen extension is required");
    CHECK_MESSAGE(!sokoban::screenIndexFromFilename(
              "screen999999999999999999999999.scr").has_value(),
        "overflowing screen index is rejected");

    const std::filesystem::path root = "project/levels";
    const std::filesystem::path level =
        sokoban::levelDirectoryPath(root, 12);
    const std::filesystem::path screen =
        sokoban::screenFilePath(level, 3);
    CHECK_MESSAGE(level == root / "level12",
        "level path uses canonical unpadded name");
    CHECK_MESSAGE(screen == root / "level12/screen3.scr",
        "screen path uses canonical unpadded name");
    CHECK_MESSAGE((sokoban::levelLocationFromScreenPath(screen) ==
            sokoban::LevelLocation { .level = 12, .screen = 3 }),
        "screen path resolves to its puzzle location");
    CHECK_MESSAGE((sokoban::levelLocationFromScreenPath(
              "project/levels/level02/screen003.scr") ==
            sokoban::LevelLocation { .level = 2, .screen = 3 }),
        "path interpretation matches accepted numeric spelling");
    CHECK_MESSAGE(!sokoban::levelLocationFromScreenPath(
              "project/levels/overworld/screen3.scr").has_value(),
        "overworld screen is not a puzzle location");
}

void testOptionalLevelMetadataRoundTrips()
{
    const ScopedTestDirectory temporary("sokoban-level-metadata-tests");
    const std::filesystem::path& directory = temporary.path();

    const sokoban::LevelMetadata fallback =
        sokoban::loadLevelMetadata(directory, 2);
    CHECK_MESSAGE(fallback.name.empty(), "missing metadata has no level name");
    CHECK_MESSAGE(fallback.screenNames == std::vector<std::string>({ "", "" }),
        "missing metadata provides one fallback name per screen");

    const sokoban::LevelMetadata authored {
        .name = "Sunken Courtyard",
        .screenNames = { "The Gate", "Flooded Steps" },
    };
    sokoban::writeLevelMetadata(directory, authored);
    CHECK_MESSAGE(sokoban::loadLevelMetadata(directory, 2) == authored,
        "authored level and screen names round trip");
}

} // namespace

int main()
{
    testValidSavedLocationIsPreserved();
    testCatalogBoundaries();
    testInvalidSavedLocationFallsBackToStart();
    testPuzzlePathConvention();
    testOptionalLevelMetadataRoundTrips();

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << checks << " checks passed\n";
    return 0;
}
