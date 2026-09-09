#include "engine/LevelCatalog.hpp"

#include <chrono>
#include <filesystem>
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

void testPuzzlePathConvention()
{
    check(sokoban::levelIndexFromDirectoryName("level0") == 0,
        "zero level index parses");
    check(sokoban::levelIndexFromDirectoryName("level0042") == 42,
        "numeric level spelling parses");
    check(!sokoban::levelIndexFromDirectoryName("level").has_value(),
        "level name requires an index");
    check(!sokoban::levelIndexFromDirectoryName("level-1").has_value(),
        "negative level index is rejected");
    check(!sokoban::levelIndexFromDirectoryName("Level1").has_value(),
        "level prefix is case-sensitive");

    check(sokoban::screenIndexFromFilename("screen3.scr") == 3,
        "screen filename parses");
    check(!sokoban::screenIndexFromFilename("screen3.txt").has_value(),
        "screen extension is required");
    check(!sokoban::screenIndexFromFilename(
              "screen999999999999999999999999.scr").has_value(),
        "overflowing screen index is rejected");

    const std::filesystem::path root = "project/levels";
    const std::filesystem::path level =
        sokoban::levelDirectoryPath(root, 12);
    const std::filesystem::path screen =
        sokoban::screenFilePath(level, 3);
    check(level == root / "level12",
        "level path uses canonical unpadded name");
    check(screen == root / "level12/screen3.scr",
        "screen path uses canonical unpadded name");
    check(sokoban::levelLocationFromScreenPath(screen) ==
            sokoban::LevelLocation { .level = 12, .screen = 3 },
        "screen path resolves to its puzzle location");
    check(sokoban::levelLocationFromScreenPath(
              "project/levels/level02/screen003.scr") ==
            sokoban::LevelLocation { .level = 2, .screen = 3 },
        "path interpretation matches accepted numeric spelling");
    check(!sokoban::levelLocationFromScreenPath(
              "project/levels/overworld/screen3.scr").has_value(),
        "overworld screen is not a puzzle location");
}

void testOptionalLevelMetadataRoundTrips()
{
    const auto unique =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("sokoban_level_metadata_tests_" + std::to_string(unique));
    std::filesystem::create_directories(directory);

    const sokoban::LevelMetadata fallback =
        sokoban::loadLevelMetadata(directory, 2);
    check(fallback.name.empty(), "missing metadata has no level name");
    check(fallback.screenNames == std::vector<std::string>({ "", "" }),
        "missing metadata provides one fallback name per screen");

    const sokoban::LevelMetadata authored {
        .name = "Sunken Courtyard",
        .screenNames = { "The Gate", "Flooded Steps" },
    };
    sokoban::writeLevelMetadata(directory, authored);
    check(sokoban::loadLevelMetadata(directory, 2) == authored,
        "authored level and screen names round trip");

    std::filesystem::remove_all(directory);
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
