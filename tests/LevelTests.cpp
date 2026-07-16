// Headless tests for the .scr parser, serializer, normalization, and queries.

#include "engine/Level.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
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

void checkThrowsContaining(const std::function<void()>& operation, std::string_view expected)
{
    try {
        operation();
        CHECK(false);
    } catch (const std::runtime_error& error) {
        CHECK(std::string_view(error.what()).find(expected) != std::string_view::npos);
    } catch (...) {
        CHECK(false);
    }
}

void testLegacyAndLayeredParsing()
{
    TEST("legacyAndLayeredParsing");
    const std::vector<std::string> legacy { "C.", " #" };
    const Level::LayerRows legacyLayers = Level::parseLayerRows(legacy, "legacy");
    CHECK(legacyLayers.size() == 1);
    CHECK(legacyLayers[0] == legacy);

    const std::vector<std::string> layered {
        "",
        "@layer 0",
        "...",
        "...",
        "",
        "@layer 1",
        "C R",
        "",
    };
    const Level::LayerRows layers = Level::parseLayerRows(layered, "layered");
    CHECK(layers.size() == 2);
    CHECK(layers[0] == std::vector<std::string>({ "...", "..." }));
    CHECK(layers[1] == std::vector<std::string>({ "C R" }));
}

void testSerializationRoundTrip()
{
    TEST("serializationRoundTrip");
    const Level::LayerRows single { { "C.", " #" } };
    CHECK(Level::serializeLayerRows(single) == single[0]);

    const Level::LayerRows layered {
        { "....", ".." },
        { "C R", "  #" },
    };
    const std::vector<std::string> serialized = Level::serializeLayerRows(layered);
    CHECK(serialized.front() == "@layer 0");
    CHECK(serialized[3].empty());
    CHECK(serialized[4] == "@layer 1");
    CHECK(Level::parseLayerRows(serialized, "round trip") == layered);
}

void testParserRejectsMalformedStructure()
{
    TEST("parserRejectsMalformedStructure");
    checkThrowsContaining([] { (void)Level::parseLayerRows({}, "empty"); }, "Level is empty");
    checkThrowsContaining([] {
        (void)Level::parseLayerRows({ "@layer 1", "C" }, "bad start");
    }, "sequential");
    checkThrowsContaining([] {
        (void)Level::parseLayerRows({ "@layer 0", "C", "@layer 2", "." }, "gap");
    }, "sequential");
    checkThrowsContaining([] {
        (void)Level::parseLayerRows({ "C", "@layer 0", "." }, "early data");
    }, "before '@layer 0'");
    checkThrowsContaining([] {
        (void)Level::parseLayerRows({ "@layer 0", "C", "@layer 1", "" }, "empty layer");
    }, "Every layer");
}

void testLevelValidationErrors()
{
    TEST("levelValidationErrors");
    checkThrowsContaining([] {
        (void)Level::loadFromLayers({}, "no layers");
    }, "no layers");
    checkThrowsContaining([] {
        (void)Level::loadFromLayers({ { "" } }, "no tiles");
    }, "no tiles");
    checkThrowsContaining([] {
        (void)Level::loadFromLayers({ { "..." } }, "no player");
    }, "missing a player");
    checkThrowsContaining([] {
        (void)Level::loadFromLayers({ { "CC" } }, "two players");
    }, "more than one player");
    checkThrowsContaining([] {
        (void)Level::loadFromLayers({ { "C?" } }, "unknown tile");
    }, "Unknown level tile");
    checkThrowsContaining([] {
        (void)Level::loadFromLayers({ { "..." }, { "LC " } }, "unsupported ladder");
    }, "same layer");
}

void testRaggedLayersNormalizeToAir()
{
    TEST("raggedLayersNormalizeToAir");
    const Level level = Level::loadFromLayers({
        { "....", ".." },
        { "C", " R#" },
    }, "ragged");

    CHECK(level.width() == 4);
    CHECK(level.height() == 2);
    CHECK(level.depth() == 2);
    CHECK(level.playerStart() == GridPosition3({ 0, 0, 1 }));
    CHECK(level.movableTiles().size() == 1);
    CHECK(level.movableTiles()[0].position == GridPosition3({ 1, 1, 1 }));
    CHECK(level.tileAt(0, 0, 1) == TileType::Air);
    CHECK(level.tileAt(1, 1, 1) == TileType::Air);
    CHECK(level.tileAt(3, 1, 1) == TileType::Air);
    CHECK(level.supportingTileAt({ 0, 0, 1 }) == TileType::Ground);
    CHECK(level.isWalkable({ 0, 0, 1 }));
    CHECK(!level.isWalkable({ 3, 1, 1 }));
    CHECK(!level.inBounds({ -1, 0, 0 }));
    CHECK(!level.supportingTileAt({ 0, 0, 0 }));
}

void testLadderRequiresSameLayerGround()
{
    TEST("ladderRequiresSameLayerGround");
    const Level level = Level::loadFromLayers({
        { "..." },
        { "L.C" },
    }, "valid ladder");
    CHECK(level.tileAt(0, 0, 1) == TileType::Ladder);
    CHECK(level.tileAt(1, 0, 1) == TileType::Ground);
}

void testFileLoadingHandlesCrLfAndMissingFiles()
{
    TEST("fileLoadingHandlesCrLfAndMissingFiles");
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("sokoban_level_tests_" + std::to_string(unique));
    const std::filesystem::path path = root / "windows.scr";
    std::filesystem::create_directories(root);
    {
        std::ofstream file(path, std::ios::binary);
        file << "@layer 0\r\n..\r\n\r\n@layer 1\r\nC \r\n";
    }

    const Level level = Level::loadFromFile(path);
    CHECK(level.width() == 2);
    CHECK(level.height() == 1);
    CHECK(level.depth() == 2);
    CHECK(level.playerStart() == GridPosition3({ 0, 0, 1 }));
    checkThrowsContaining([&] {
        (void)Level::loadFromFile(root / "missing.scr");
    }, "Failed to open");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

} // namespace

int main()
{
    testLegacyAndLayeredParsing();
    testSerializationRoundTrip();
    testParserRejectsMalformedStructure();
    testLevelValidationErrors();
    testRaggedLayersNormalizeToAir();
    testLadderRequiresSameLayerGround();
    testFileLoadingHandlesCrLfAndMissingFiles();

    if (failures == 0) {
        std::cout << "LevelTests: " << checks << " checks passed\n";
        return 0;
    }

    std::cerr << "LevelTests: " << failures << " of " << checks << " checks failed\n";
    return 1;
}
