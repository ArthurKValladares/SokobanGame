// Headless tests for the .scr parser, serializer, normalization, and queries.

#include "TestHarness.hpp"

#include "engine/Level.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace sokoban;

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

void testCameraMetadataRoundTripAndValidation()
{
    TEST("cameraMetadataRoundTripAndValidation");
    const CameraAngles angles { .pitchDegrees = 52.5f, .yawDegrees = -125.0f };
    const Level::Definition definition {
        .layers = { { "C." } },
        .cameraAngles = angles,
    };
    const auto lines = Level::serializeDefinition(definition);
    CHECK(lines.front().starts_with("@camera "));
    const auto parsed = Level::parseDefinition(lines, "camera round trip");
    CHECK(parsed == definition);
    CHECK(Level::loadFromDefinition(parsed, "camera").cameraAngles() == angles);
    CHECK(!Level::loadFromLines({ "C." }, "legacy").cameraAngles());
    CHECK(Level::serializeDefinition({ .layers = { { "C." } } }) ==
        std::vector<std::string> { "C." });

    for (const std::string payload : {
             "{}", "[]", "{\"pitch\":30}", "{\"pitch\":true,\"yaw\":0}",
             "{\"pitch\":\"30\",\"yaw\":0}", "{\"pitch\":-1,\"yaw\":0}",
             "{\"pitch\":90,\"yaw\":0}", "{\"pitch\":30,\"yaw\":181}",
             "{\"pitch\":30,\"yaw\":-181}", "{\"pitch\":1e100,\"yaw\":0}",
             "{invalid}" }) {
        checkThrowsContaining([&] {
            (void)Level::parseDefinition({ "@camera " + payload, "@layer 0", "C." }, "bad camera");
        }, "amera");
    }
    checkThrowsContaining([&] {
        (void)Level::parseDefinition({ lines.front(), lines.front(), "@layer 0", "C." }, "duplicate camera");
    }, "more than one '@camera'");
    checkThrowsContaining([&] {
        (void)Level::parseDefinition({ "@layer 0", "C.", lines.front() }, "late camera");
    }, "before '@layer 0'");
    checkThrowsContaining([&] {
        (void)Level::parseDefinition({ lines.front(), "C." }, "missing layers");
    }, "requires explicit");
    Level::Definition invalid = definition;
    invalid.cameraAngles->pitchDegrees = std::numeric_limits<float>::infinity();
    checkThrowsContaining([&] { (void)Level::serializeDefinition(invalid); }, "finite");
    checkThrowsContaining([&] { (void)Level::loadFromDefinition(invalid, "bad camera"); }, "finite");
    for (CameraAngles boundary : { CameraAngles { 0.0f, -180.0f }, CameraAngles { 89.0f, 180.0f } }) {
        invalid.cameraAngles = boundary;
        CHECK(Level::parseDefinition(Level::serializeDefinition(invalid), "boundary") == invalid);
    }
}

void testCharacterMetadataRoundTripAndLegacyDefault()
{
    TEST("characterMetadataRoundTripAndLegacyDefault");
    const Level::Definition definition {
        .layers = {
            { "..." },
            { "C  " },
        },
        .character = CharacterType::Knight,
    };
    const std::vector<std::string> serialized =
        Level::serializeDefinition(definition);
    CHECK(serialized[0] == "@character knight");
    CHECK(serialized[1].empty());

    const Level::Definition parsed =
        Level::parseDefinition(serialized, "knight round trip");
    CHECK(parsed == definition);
    CHECK(Level::loadFromDefinition(parsed, "knight").character() ==
        CharacterType::Knight);

    Level::Definition druidDefinition = definition;
    druidDefinition.character = CharacterType::Druid;
    const std::vector<std::string> druidSerialized =
        Level::serializeDefinition(druidDefinition);
    CHECK(druidSerialized[0] == "@character druid");
    CHECK(Level::parseDefinition(druidSerialized, "druid round trip") ==
        druidDefinition);

    Level::Definition witchDefinition = definition;
    witchDefinition.character = CharacterType::Witch;
    const std::vector<std::string> witchSerialized =
        Level::serializeDefinition(witchDefinition);
    CHECK(witchSerialized[0] == "@character witch");
    CHECK(Level::parseDefinition(witchSerialized, "witch round trip") ==
        witchDefinition);

    Level::Definition bardDefinition = definition;
    bardDefinition.character = CharacterType::Bard;
    const std::vector<std::string> bardSerialized =
        Level::serializeDefinition(bardDefinition);
    CHECK(bardSerialized[0] == "@character bard");
    CHECK(Level::parseDefinition(bardSerialized, "bard round trip") ==
        bardDefinition);

    Level::Definition lorekeeperDefinition = definition;
    lorekeeperDefinition.character = CharacterType::Lorekeeper;
    const std::vector<std::string> lorekeeperSerialized =
        Level::serializeDefinition(lorekeeperDefinition);
    CHECK(lorekeeperSerialized[0] == "@character lorekeeper");
    CHECK(Level::parseDefinition(
        lorekeeperSerialized, "lorekeeper round trip") == lorekeeperDefinition);

    const Level legacy = Level::loadFromLines({ "C." }, "legacy rogue");
    CHECK(legacy.character() == CharacterType::Rogue);
}

void testWardrobeVariantsRoundTripAndRemainTraversable()
{
    TEST("wardrobeVariantsRoundTripAndRemainTraversable");
    const Level::Definition definition {
        .layers = {
            { "......." },
            { "C09+*/?" },
        },
        .character = CharacterType::Lorekeeper,
    };
    const std::vector<std::string> serialized =
        Level::serializeDefinition(definition);
    CHECK(Level::parseDefinition(serialized, "wardrobe round trip") ==
        definition);

    const Level level = Level::loadFromDefinition(definition, "wardrobes");
    const std::array<CharacterType, 6> characters {
        CharacterType::Lorekeeper,
        CharacterType::Rogue,
        CharacterType::Knight,
        CharacterType::Druid,
        CharacterType::Witch,
        CharacterType::Bard,
    };
    CHECK(level.wardrobes().size() == characters.size());
    for (std::size_t i = 0; i < characters.size(); ++i) {
        const GridPosition3 position { static_cast<int>(i + 1), 0, 1 };
        CHECK(level.isWalkable(position));
        const Level::Wardrobe* wardrobe = level.wardrobeAt(position);
        CHECK(wardrobe != nullptr);
        CHECK(wardrobe && wardrobe->character == characters[i]);
        CHECK(wardrobeTileForCharacter(characters[i]) ==
            level.tileAt(position.x, position.y, position.z));
    }
}

void testStoneWallVariantsRoundTripAndSupportUnits()
{
    TEST("stoneWallVariantsRoundTripAndSupportUnits");
    const Level::Definition definition {
        .layers = {
            { "........." },
            { "C#dfhikmr" },
            { "         " },
        },
    };
    const auto serialized = Level::serializeDefinition(definition);
    CHECK(Level::parseDefinition(serialized, "stone wall round trip") == definition);
    const Level level = Level::loadFromLines(serialized, "stone walls");
    const std::array walls {
        TileType::Wall, TileType::WallStone02, TileType::WallStone03,
        TileType::WallStone04, TileType::WallStone05, TileType::WallStone06,
        TileType::WallStone07, TileType::WallStone08,
    };
    CHECK(walls.size() == wallStoneVariantCount);
    CHECK(tileTypeToChar(TileType::Wall) == '#');
    for (std::size_t i = 0; i < walls.size(); ++i) {
        const int x = static_cast<int>(i + 1);
        const TileType tile = walls[i];
        CHECK(level.tileAt(x, 0, 1) == tile);
        CHECK(charToTileType(tileTypeToChar(tile)) == tile);
        CHECK(tileTypeFromName(tileTypeName(tile)) == tile);
        CHECK(tileTypeIsWall(tile));
        CHECK(wallStoneVariantFor(tile) == i);
        CHECK(tileTypeIsSolidBlock(tile));
        CHECK(tileTypeSupportsEntity(tile));
        CHECK(!tileTypeAllowsEntity(tile));
        CHECK(!tileTypeIsGround(tile));
        CHECK(!level.isWalkable({ x, 0, 1 }));
        CHECK(level.isWalkable({ x, 0, 2 }));
        CHECK(level.supportingTileAt({ x, 0, 2 }) == tile);
    }
    CHECK(!tileTypeIsWall(TileType::Ground));
    CHECK(!tileTypeIsWall(TileType::Rock));
}

void testWaterLayerMetadataAndTileResolution()
{
    TEST("waterLayerMetadataAndTileResolution");
    const std::vector<std::string> lines {
        "@water 0",
        "",
        "@layer 0",
        " . ",
        "",
        "@layer 1",
        "C  ",
    };
    const Level::Definition definition =
        Level::parseDefinition(lines, "water metadata");
    CHECK(definition.waterLayer == 0U);
    CHECK(definition.layers.size() == 2);
    CHECK(Level::serializeDefinition(definition) == lines);

    const Level level =
        Level::loadFromDefinition(definition, "water metadata");
    CHECK(level.waterLayer() == 0U);
    CHECK(level.authoredTileAt(0, 0, 0) == TileType::Air);
    CHECK(level.tileAt(0, 0, 0) == TileType::Water);
    CHECK(level.tileAt(1, 0, 0) == TileType::Ground);
    CHECK(level.tileAt(2, 0, 0) == TileType::Water);
    CHECK(level.width() == 3);
    CHECK(level.height() == 1);
    CHECK(level.isWalkable({ 0, 0, 1 }));
}

void testDecorationMetadataRoundTrip()
{
    TEST("decorationMetadataRoundTrip");
    const Level::Decoration decoration {
        .model = "Stone",
        .position = { 1.5f, 2.25f, 3.0f },
        .rotationDegrees = { 15.0f, -25.0f, 90.0f },
        .scale = { 0.5f, 1.25f, 2.0f },
        .pointLight = Level::Decoration::PointLight {
            .offset = { 0.1f, -0.2f, 0.8f },
            .color = { 0.25f, 0.5f, 1.0f },
            .intensity = 3.5f,
            .range = 7.0f,
            .castsShadows = true,
            .shadowBias = 0.004f,
            .shadowOpacity = 0.7f,
        },
    };
    const Level::Definition definition {
        .layers = {
            { "..." },
            { "C  " },
        },
        .decorations = { decoration },
    };

    const std::vector<std::string> serialized =
        Level::serializeDefinition(definition);
    CHECK(serialized.front().starts_with("@decoration {"));
    CHECK(serialized.front().find("\"light\"") != std::string::npos);
    CHECK(serialized[1].empty());
    CHECK(serialized[2] == "@layer 0");
    const Level::Definition parsed =
        Level::parseDefinition(serialized, "decoration round trip");
    CHECK(parsed == definition);

    const Level level =
        Level::loadFromDefinition(parsed, "decoration round trip");
    CHECK(level.decorations().size() == 1);
    CHECK(level.decorations().front() == decoration);
    CHECK(level.tileAt(1, 0, 1) == TileType::Air);
    CHECK(level.isWalkable({ 1, 0, 1 }));
}

void testSelectorMetadataRoundTripAndLookup()
{
    TEST("selectorMetadataRoundTripAndLookup");
    const Level::Definition definition {
        .layers = {
            { "..." },
            { "C  " },
        },
        .selectors = {
            {
                .id = 1,
                .cell = { 1, 0, 1 },
                .target = LevelLocation { .level = 2, .screen = 3 },
            },
            {
                .id = 4,
                .cell = { 2, 0, 1 },
                .target = std::nullopt,
            },
        },
    };

    const std::vector<std::string> serialized =
        Level::serializeDefinition(definition);
    CHECK(serialized[0] ==
        "@selector {\"cell\":[1,0,1],\"id\":1,\"target\":{\"level\":2,\"screen\":3}}");
    CHECK(serialized[1] ==
        "@selector {\"cell\":[2,0,1],\"id\":4,\"target\":null}");
    CHECK(serialized[2].empty());

    const Level::Definition parsed =
        Level::parseDefinition(serialized, "selector round trip");
    CHECK(parsed == definition);

    const Level level =
        Level::loadFromDefinition(parsed, "selector round trip");
    CHECK(level.selectors().size() == 2);
    CHECK(level.selectorAt({ 1, 0, 1 }) != nullptr);
    CHECK(level.selectorAt({ 1, 0, 1 })->id == 1);
    CHECK(level.selectorAt({ 1, 0, 1 })->target ==
        std::optional<LevelLocation>({ .level = 2, .screen = 3 }));
    CHECK(level.selectorAt({ 0, 0, 1 }) == nullptr);
    CHECK(level.tileAt(1, 0, 1) == TileType::Air);
    CHECK(level.isWalkable({ 1, 0, 1 }));
}

void testGateMetadataRoundTripAndValidation()
{
    TEST("gateMetadataRoundTripAndValidation");
    const Level::Definition definition {
        .layers = {
            { "...." },
            { "CPG " },
        },
        .gates = { Level::Gate {
            .cell = { 2, 0, 1 },
            .pressurePlates = { { 1, 0, 1 } },
            .color = { 0.2f, 0.65f, 1.0f },
        } },
    };
    const std::vector<std::string> serialized =
        Level::serializeDefinition(definition);
    CHECK(serialized.front().starts_with("@gate "));
    const Level::Definition parsed =
        Level::parseDefinition(serialized, "gate round trip");
    CHECK(parsed == definition);

    const Level level = Level::loadFromDefinition(parsed, "gate level");
    CHECK(tileTypeToChar(TileType::Gate) == 'G');
    CHECK(charToTileType('G') == TileType::Gate);
    CHECK(level.gates().size() == 1);
    CHECK(level.gateAt({ 2, 0, 1 }) == &level.gates().front());
    CHECK(level.gateForPressurePlate({ 1, 0, 1 }) ==
        &level.gates().front());
    CHECK(!level.isWalkable({ 2, 0, 1 }));

    checkThrowsContaining([] {
        (void)Level::loadFromDefinition({
            .layers = {
                { "..." },
                { "C G" },
            },
        }, "gate missing metadata");
    }, "requires an '@gate'");
    checkThrowsContaining([] {
        (void)Level::loadFromDefinition({
            .layers = {
                { "..." },
                { "C.G" },
            },
            .gates = { Level::Gate {
                .cell = { 2, 0, 1 },
                .pressurePlates = { { 1, 0, 1 } },
            } },
        }, "gate bad link");
    }, "Pressure or Button tiles");
}

void testRotatorMetadataRoundTripAndValidation()
{
    TEST("rotatorMetadataRoundTripAndValidation");
    const Level::Definition definition {
        .layers = {
            { "....." },
            { "CP)(G" },
        },
        .gates = { Level::Gate {
            .cell = { 4, 0, 1 },
            .pressurePlates = { { 1, 0, 1 } },
        } },
        .rotators = {
            Level::Rotator {
                .cell = { 2, 0, 1 },
                .pressurePlates = { { 1, 0, 1 } },
                .color = { 0.9f, 0.3f, 0.2f },
            },
            Level::Rotator { .cell = { 3, 0, 1 } },
        },
    };
    const std::vector<std::string> serialized =
        Level::serializeDefinition(definition);
    CHECK(std::ranges::count_if(serialized, [](const std::string& line) {
        return line.starts_with("@rotator ");
    }) == 2);
    const Level::Definition parsed =
        Level::parseDefinition(serialized, "rotator round trip");
    CHECK(parsed == definition);

    const Level level = Level::loadFromDefinition(parsed, "rotator level");
    CHECK(tileTypeToChar(TileType::RotatorClockwise) == ')');
    CHECK(tileTypeToChar(TileType::RotatorCounterClockwise) == '(');
    CHECK(charToTileType(')') == TileType::RotatorClockwise);
    CHECK(charToTileType('(') == TileType::RotatorCounterClockwise);
    CHECK(rotatorQuarterTurns(TileType::RotatorClockwise) == 1);
    CHECK(rotatorQuarterTurns(TileType::RotatorCounterClockwise) == -1);
    CHECK(!rotatorQuarterTurns(TileType::PressurePlate));
    CHECK(level.rotators().size() == 2);
    CHECK(level.rotatorAt({ 2, 0, 1 }) == &level.rotators().front());
    CHECK(level.rotatorForPressurePlate({ 1, 0, 1 }) ==
        &level.rotators().front());
    CHECK(level.isWalkable({ 2, 0, 1 }));
    CHECK(level.isWalkable({ 3, 0, 1 }));
    // A plate linked to both a gate and a rotator takes the gate's color.
    CHECK(level.pressurePlateLinkColor({ 1, 0, 1 }) ==
        std::optional<Vec3>(level.gates().front().color));

    checkThrowsContaining([] {
        (void)Level::loadFromDefinition({
            .layers = {
                { "..." },
                { "C )" },
            },
        }, "rotator missing metadata");
    }, "requires an '@rotator'");
    checkThrowsContaining([] {
        (void)Level::loadFromDefinition({
            .layers = {
                { "..." },
                { "C.)" },
            },
            .rotators = { Level::Rotator {
                .cell = { 2, 0, 1 },
                .pressurePlates = { { 1, 0, 1 } },
            } },
        }, "rotator bad link");
    }, "Pressure or Button tiles");
    checkThrowsContaining([] {
        (void)Level::loadFromDefinition({
            .layers = {
                { "..." },
                { "CP." },
            },
            .rotators = { Level::Rotator {
                .cell = { 2, 0, 1 },
                .pressurePlates = { { 1, 0, 1 } },
            } },
        }, "rotator record without tile");
    }, "must contain a Rotator tile");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            {
                "@layer 0",
                "...",
                "@rotator {\"cell\":[2,0,1],\"plates\":[],\"color\":[2,0,0]}",
            },
            "rotator after layer");
    }, "before '@layer 0'");
}

void testElevatorMetadataRoundTripAndValidation()
{
    TEST("elevatorMetadataRoundTripAndValidation");
    const Level::Definition definition {
        .layers = {
            { "....=" },
            { "CP.  " },
            { "     " },
            { "     " },
        },
        .elevators = { Level::Elevator {
            .cell = { 4, 0, 0 },
            .pressurePlates = { { 1, 0, 1 } },
            .color = { 0.2f, 0.4f, 0.9f },
            .levels = { 0, 3, 2 },
        } },
    };
    const std::vector<std::string> serialized =
        Level::serializeDefinition(definition);
    const auto record = std::ranges::find_if(
        serialized,
        [](const std::string& line) { return line.starts_with("@elevator "); });
    CHECK(record != serialized.end());
    // Stops keep their authored travel order.
    CHECK(record != serialized.end() &&
        record->find("\"levels\":[0,3,2]") != std::string::npos);
    const Level::Definition parsed =
        Level::parseDefinition(serialized, "elevator round trip");
    CHECK(parsed == definition);

    const Level level = Level::loadFromDefinition(parsed, "elevator level");
    CHECK(tileTypeToChar(TileType::Elevator) == '=');
    CHECK(charToTileType('=') == TileType::Elevator);
    CHECK(tileTypeIsElevator(TileType::Elevator));
    CHECK(tileTypeIsSolidBlock(TileType::Elevator));
    CHECK(!tileTypeAllowsEntity(TileType::Elevator));
    CHECK(level.elevators().size() == 1);
    CHECK(level.elevatorAt({ 4, 0, 0 }) == &level.elevators().front());
    CHECK(level.elevatorForPressurePlate({ 1, 0, 1 }) ==
        &level.elevators().front());
    CHECK(level.elevators().front().startStop() == 0);
    CHECK(level.pressurePlateLinkColor({ 1, 0, 1 }) ==
        std::optional<Vec3>(definition.elevators.front().color));
    // The platform's starting cell supports a unit like any block.
    CHECK(level.isWalkable({ 4, 0, 1 }));

    const auto loadWith = [](std::vector<int> levels, std::string top) {
        return [levels = std::move(levels), top = std::move(top)] {
            (void)Level::loadFromDefinition({
                .layers = {
                    { "..=" },
                    { "CP " },
                    { std::string(top) },
                },
                .elevators = { Level::Elevator {
                    .cell = { 2, 0, 0 },
                    .pressurePlates = { { 1, 0, 1 } },
                    .levels = levels,
                } },
            }, "elevator validation");
        };
    };
    checkThrowsContaining(loadWith({ 1, 2 }, "   "), "include the layer");
    checkThrowsContaining(loadWith({ 0, 3 }, "   "), "existing layers");
    checkThrowsContaining(loadWith({ 0, 2, 0 }, "   "), "more than once");
    checkThrowsContaining(loadWith({}, "   "), "at least one stop");
    checkThrowsContaining(loadWith({ 0, -1 }, "   "), "must not be negative");
    checkThrowsContaining([] {
        (void)Level::loadFromDefinition({
            .layers = { { "..=" }, { "C  " } },
        }, "elevator missing metadata");
    }, "requires an '@elevator'");
    checkThrowsContaining([] {
        (void)Level::loadFromDefinition({
            .layers = { { "..." }, { "CP " } },
            .elevators = { Level::Elevator {
                .cell = { 2, 0, 0 },
                .levels = { 0 },
            } },
        }, "elevator record without tile");
    }, "must contain an Elevator tile");
    checkThrowsContaining([] {
        (void)Level::loadFromDefinition({
            .layers = { { "..=" }, { "C. " } },
            .elevators = { Level::Elevator {
                .cell = { 2, 0, 0 },
                .pressurePlates = { { 1, 0, 1 } },
                .levels = { 0 },
            } },
        }, "elevator bad link");
    }, "Pressure or Button tiles");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            {
                "@elevator {\"cell\":[2,0,0],\"plates\":[],\"color\":[1,1,1]}",
                "@layer 0",
                "..=",
            },
            "elevator without levels");
    }, "'levels'");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            {
                "@layer 0",
                "..=",
                "@elevator {\"cell\":[2,0,0],\"plates\":[],\"color\":[1,1,1],\"levels\":[0]}",
            },
            "elevator after layer");
    }, "before '@layer 0'");
}

void testGateStartOpenRoundTrips()
{
    TEST("gateStartOpenRoundTrips");
    const Level::Definition definition {
        .layers = { { "...." }, { "CPGG" } },
        .gates = {
            Level::Gate {
                .cell = { 2, 0, 1 },
                .pressurePlates = { { 1, 0, 1 } },
            },
            Level::Gate {
                .cell = { 3, 0, 1 },
                .pressurePlates = { { 1, 0, 1 } },
                .startOpen = true,
            },
        },
    };
    const std::vector<std::string> serialized =
        Level::serializeDefinition(definition);
    // Only the start-open gate writes the field, so older screens are
    // unchanged by a save.
    CHECK(std::ranges::count_if(serialized, [](const std::string& line) {
        return line.find("startOpen") != std::string::npos;
    }) == 1);
    const Level::Definition parsed =
        Level::parseDefinition(serialized, "start-open round trip");
    CHECK(parsed == definition);
    const Level level = Level::loadFromDefinition(parsed, "start-open level");
    CHECK(!level.gateAt({ 2, 0, 1 })->startOpen);
    CHECK(level.gateAt({ 3, 0, 1 })->startOpen);

    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            {
                "@gate {\"cell\":[1,0,0],\"plates\":[],\"color\":[1,1,1],\"startOpen\":1}",
                "@layer 0",
                ".G",
            },
            "start-open not a boolean");
    }, "'startOpen'");
}

void testEditorLinkColorsRoundTripAndDoNotAffectGameplay()
{
    TEST("editorLinkColorsRoundTripAndDoNotAffectGameplay");
    const Level::Definition definition {
        .layers = { { "...." }, { "CPPG" } },
        .gates = { Level::Gate {
            .cell = { 3, 0, 1 },
            .pressurePlates = { { 1, 0, 1 } },
        } },
        .linkColors = { Level::LinkColor {
            .cell = { 2, 0, 1 },
            .color = { 0.1f, 0.9f, 0.2f },
        } },
    };
    const std::vector<std::string> serialized =
        Level::serializeDefinition(definition);
    CHECK(std::ranges::count_if(serialized, [](const std::string& line) {
        return line.starts_with("@linkcolor ");
    }) == 1);
    const Level::Definition parsed =
        Level::parseDefinition(serialized, "link color round trip");
    CHECK(parsed == definition);
    // Gameplay reads only the explicit plate list.
    const Level level = Level::loadFromDefinition(parsed, "link color level");
    CHECK(level.gateAt({ 3, 0, 1 })->pressurePlates ==
        (std::vector<GridPosition3> { { 1, 0, 1 } }));
    CHECK(!level.pressurePlateLinkColor(GridPosition3 { 2, 0, 1 }).has_value());

    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            {
                "@linkcolor {\"cell\":[1,0,1],\"color\":[2,0,0]}",
                "@layer 0",
                "...",
            },
            "bad link color");
    }, "from zero to one");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            {
                "@layer 0",
                "...",
                "@linkcolor {\"cell\":[1,0,1],\"color\":[1,0,0]}",
            },
            "late link color");
    }, "before '@layer 0'");
}

void testObjectLinksRoundTripAndValidateMovableCells()
{
    TEST("objectLinksRoundTripAndValidateMovableCells");
    const Vec3 blue { 0.2f, 0.4f, 1.0f };
    const Level::Definition definition {
        .layers = { { "....." }, { "CR n " } },
        .objectLinks = {
            { .cell = { 1, 0, 1 }, .color = blue },
            { .cell = { 3, 0, 1 }, .color = blue },
        },
    };
    const std::vector<std::string> serialized =
        Level::serializeDefinition(definition);
    CHECK(std::ranges::count_if(serialized, [](const std::string& line) {
        return line.starts_with("@objectlink ");
    }) == 2);
    const Level::Definition parsed =
        Level::parseDefinition(serialized, "object link round trip");
    CHECK(parsed == definition);

    const Level level = Level::loadFromDefinition(parsed, "object link level");
    CHECK(level.objectLinks() == definition.objectLinks);
    CHECK(level.movableLinkColor(0) == std::optional<Vec3>(blue));
    CHECK(level.movableLinkColor(1) == std::optional<Vec3>(blue));
    CHECK(level.movablesAreLinked(0, 1));
    CHECK(!level.movablesAreLinked(0, 99));

    checkThrowsContaining([] {
        (void)Level::loadFromDefinition({
            .layers = { { "..." }, { "C  " } },
            .objectLinks = {
                { .cell = { 1, 0, 1 }, .color = { 1.0f, 0.0f, 0.0f } },
            },
        }, "object link without object");
    }, "must contain a movable object");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            {
                "@objectlink {\"cell\":[1,0,1],\"color\":[2,0,0]}",
                "@layer 0",
                "...",
            },
            "bad object link color");
    }, "from zero to one");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            {
                "@layer 0",
                "...",
                "@objectlink {\"cell\":[1,0,1],\"color\":[1,0,0]}",
            },
            "late object link");
    }, "before '@layer 0'");
}

void testPlatePropertyAndCoveredPlateRecords()
{
    TEST("platePropertyAndCoveredPlateRecords");
    for (const TileType plate : {
             TileType::PressurePlate,
             TileType::End,
             TileType::RotatorClockwise,
             TileType::RotatorCounterClockwise,
         }) {
        CHECK(tileTypeIsPlate(plate));
        CHECK(tileTypeHasProperty(plate, TileProperty::Plate));
        CHECK(tileTypeIsSurfaceEntity(plate));
        CHECK(!tileTypeCanStandOnPlate(plate));
    }
    for (const TileType other : {
             TileType::Ground, TileType::Wall, TileType::Gate,
             TileType::Rock, TileType::MirrorNorthWest, TileType::Rogue,
         }) {
        CHECK(!tileTypeIsPlate(other));
    }
    for (const TileType occupant : {
             TileType::Rock, TileType::Ice, TileType::Enemy,
             TileType::TurretEast, TileType::Knight, TileType::Player,
             TileType::MirrorSouthEast,
         }) {
        CHECK(tileTypeCanStandOnPlate(occupant));
    }
    CHECK(!tileTypeCanStandOnPlate(TileType::Wall));
    CHECK(!tileTypeCanStandOnPlate(TileType::Air));
    CHECK(tileTypeFromName("Rotator Counter-Clockwise") ==
        TileType::RotatorCounterClockwise);
    CHECK(!tileTypeFromName("Not A Tile"));

    const Level::Definition definition {
        .layers = {
            { "......" },
            { "QPR1(E" },
        },
        .rotators = {
            Level::Rotator {
                .cell = { 3, 0, 1 },
                .pressurePlates = { { 1, 0, 1 }, { 2, 0, 1 } },
            },
            Level::Rotator { .cell = { 4, 0, 1 } },
        },
        .plates = {
            Level::Plate { .cell = { 2, 0, 1 }, .tile = TileType::PressurePlate },
            Level::Plate { .cell = { 0, 0, 1 }, .tile = TileType::End },
            Level::Plate { .cell = { 3, 0, 1 }, .tile = TileType::RotatorClockwise },
        },
    };
    const std::vector<std::string> serialized =
        Level::serializeDefinition(definition);
    CHECK(std::ranges::count_if(serialized, [](const std::string& line) {
        return line.starts_with("@plate ");
    }) == 3);
    const Level::Definition parsed =
        Level::parseDefinition(serialized, "plate round trip");
    // Parsing puts records in canonical cell order.
    CHECK(parsed.plates.size() == 3);
    CHECK(parsed.plates.front().cell == GridPosition3({ 0, 0, 1 }));
    CHECK(Level::serializeDefinition(parsed) == serialized);

    const Level level = Level::loadFromDefinition(parsed, "plate level");
    CHECK(level.coveredPlates().size() == 3);
    // A unit standing on a plate leaves the plate in the static grid.
    CHECK(level.tileAt(0, 0, 1) == TileType::End);
    CHECK(level.tileAt(2, 0, 1) == TileType::PressurePlate);
    // Mirrors leave their plate in the static grid, just like other units.
    CHECK(level.tileAt(3, 0, 1) == TileType::RotatorClockwise);
    CHECK(level.plateAt({ 3, 0, 1 }) == TileType::RotatorClockwise);
    CHECK(level.plateAt({ 4, 0, 1 }) == TileType::RotatorCounterClockwise);
    CHECK(level.plateAt({ 5, 0, 1 }) == TileType::End);
    CHECK(!level.plateAt({ 0, 0, 0 }));
    CHECK(level.pressurePlates().size() == 2);
    CHECK(level.ends().size() == 2);
    CHECK(level.movableTiles().size() == 2);
    CHECK(level.playerStarts().front().position == GridPosition3({ 0, 0, 1 }));

    const auto plateUnder = [](char occupant, TileType plate) {
        std::string row = "Q ";
        row += occupant;
        return Level::Definition {
            .layers = { { "..." }, { row } },
            .plates = { Level::Plate { .cell = { 2, 0, 1 }, .tile = plate } },
        };
    };
    // Pushing the mirror away exposes the End for a hero.
    const Level coveredEnd = Level::loadFromDefinition(
        plateUnder('1', TileType::End), "end under mirror");
    CHECK(coveredEnd.ends().size() == 1);
    CHECK(coveredEnd.plateAt({ 2, 0, 1 }) == TileType::End);

    checkThrowsContaining([&] {
        (void)Level::loadFromDefinition(
            plateUnder('#', TileType::PressurePlate), "plate under wall");
    }, "beneath a unit or mirror");
    checkThrowsContaining([&] {
        (void)Level::loadFromDefinition(
            plateUnder(' ', TileType::PressurePlate), "plate under air");
    }, "beneath a unit or mirror");
    checkThrowsContaining([&] {
        (void)Level::loadFromDefinition({
            .layers = { { "..." }, { "Q R" } },
            .plates = { Level::Plate { .cell = { 7, 0, 1 } } },
        }, "plate outside board");
    }, "beneath a unit or mirror");
    checkThrowsContaining([&] {
        (void)Level::loadFromDefinition(
            plateUnder('R', TileType::RotatorClockwise), "rotator plate without record");
    }, "requires an '@rotator'");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            {
                "@plate {\"cell\":[1,0,1],\"tile\":\"Wall\"}",
                "",
                "@layer 0",
                "..",
            },
            "non-plate record");
    }, "must name a plate tile");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            {
                "@plate {\"cell\":[1,0,1],\"tile\":\"End\"}",
                "@plate {\"cell\":[1,0,1],\"tile\":\"Pressure\"}",
                "",
                "@layer 0",
                "..",
            },
            "duplicate plate");
    }, "more than one plate record");
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
    checkThrowsContaining([] {
        (void)Level::parseDefinition({ "@water 0", "C" }, "water legacy");
    }, "requires explicit");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@decoration {}", "C" }, "decoration legacy");
    }, "requires explicit");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@selector {}", "C" }, "selector legacy");
    }, "requires explicit");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@character wizard", "@layer 0", "C" },
            "unknown character");
    }, "expected '@character lorekeeper'");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@character rogue", "@character knight", "@layer 0", "C" },
            "duplicate character");
    }, "more than one '@character'");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@layer 0", "C", "@character rogue" },
            "late character");
    }, "before '@layer 0'");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@water nope", "@layer 0", "C" },
            "bad water");
    }, "expected '@water N'");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@water 0", "@water 0", "@layer 0", "C" },
            "duplicate water");
    }, "more than one");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@water 1", "@layer 0", "C" },
            "missing water layer");
    }, "existing layer");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@layer 0", "C", "@water 0" },
            "late water");
    }, "before '@layer 0'");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@decoration not-json", "@layer 0", "C" },
            "bad decoration json");
    }, "Invalid decoration JSON");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@decoration {\"model\":\"Stone\",\"position\":[0,0,0],"
              "\"rotation\":[0,0,0],\"scale\":[1,0,1]}",
              "@layer 0", "C" },
            "zero decoration scale");
    }, "greater than zero");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@decoration {\"model\":\"Lamp\",\"position\":[0,0,0],"
              "\"rotation\":[0,0,0],\"scale\":[1,1,1],"
              "\"light\":{\"offset\":[0,0,1],\"color\":[1,1,1],"
              "\"intensity\":2,\"range\":0,\"castsShadows\":true,"
              "\"shadowBias\":0.01,\"shadowOpacity\":1}}",
              "@layer 0", "C" },
            "zero point light range");
    }, "out of range");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@layer 0", "C",
              "@decoration {\"model\":\"Stone\",\"position\":[0,0,0],"
              "\"rotation\":[0,0,0],\"scale\":[1,1,1]}" },
            "late decoration");
    }, "before '@layer 0'");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@selector not-json", "@layer 0", "C" },
            "bad selector json");
    }, "Invalid selector JSON");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@selector {\"id\":0,\"cell\":[0,0,0],\"target\":null}",
              "@layer 0", "C" },
            "zero selector id");
    }, "greater than zero");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@selector {\"id\":1,\"cell\":[0,-1,0],\"target\":null}",
              "@layer 0", "C" },
            "negative selector cell");
    }, "must not be negative");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@selector {\"id\":1,\"cell\":[0,0,0],\"target\":null}",
              "@selector {\"id\":1,\"cell\":[1,0,0],\"target\":null}",
              "@layer 0", "C." },
            "duplicate selector id");
    }, "duplicate selector id");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@selector {\"id\":1,\"cell\":[0,0,0],\"target\":null}",
              "@selector {\"id\":2,\"cell\":[0,0,0],\"target\":null}",
              "@layer 0", "C" },
            "duplicate selector cell");
    }, "more than one selector at cell");
    checkThrowsContaining([] {
        (void)Level::parseDefinition(
            { "@layer 0", "C",
              "@selector {\"id\":1,\"cell\":[0,0,0],\"target\":null}" },
            "late selector");
    }, "before '@layer 0'");
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
    }, "missing a hero");
    const Level multipleHeroes = Level::loadFromLayers(
        { { "QKUHB" } }, "multiple heroes");
    CHECK(multipleHeroes.playerStarts().size() == 5);
    CHECK(multipleHeroes.playerStarts()[0].character == CharacterType::Rogue);
    CHECK(multipleHeroes.playerStarts()[1].character == CharacterType::Knight);
    CHECK(multipleHeroes.playerStarts()[2].character == CharacterType::Druid);
    CHECK(multipleHeroes.playerStarts()[3].character == CharacterType::Witch);
    CHECK(multipleHeroes.playerStarts()[4].character == CharacterType::Bard);
    checkThrowsContaining([] {
        (void)Level::loadFromLayers({ { "C~" } }, "unknown tile");
    }, "Unknown level tile");
    checkThrowsContaining([] {
        (void)Level::loadFromLayers({ { "..." }, { "LC " } }, "unsupported ladder");
    }, "same layer");
    checkThrowsContaining([] {
        (void)Level::loadFromLayers(
            { { "." }, { "C" } },
            "unsupported selector",
            std::nullopt,
            {},
            { Level::ScreenSelector {
                .id = 1,
                .cell = { 0, 0, 0 },
            } });
    }, "supported walkable cell");
}

void testMinecartMetadataRoutesAndValidation()
{
    TEST("minecartMetadataRoutesAndValidation");
    // The shared corner GLB is authored north-west. These rotations must
    // agree with the connector masks or a track can look joined in the editor
    // while route construction sees it as disconnected.
    CHECK(railOrientationQuarterTurns(TileType::RailCornerNorthWest) == 0U);
    CHECK(railOrientationQuarterTurns(TileType::RailCornerNorthEast) == 1U);
    CHECK(railOrientationQuarterTurns(TileType::RailCornerSouthEast) == 2U);
    CHECK(railOrientationQuarterTurns(TileType::RailCornerSouthWest) == 3U);
    const Level::Definition eastDefinition {
        .layers = {
            { "......" },
            { "_-M-_C" },
        },
        .plates = {
            { .cell = { 2, 0, 1 }, .tile = TileType::RailStopEastWest },
        },
        .minecarts = { Level::Minecart {
            .cell = { 2, 0, 1 },
            .pressurePlates = {},
            .initialDirection = 1,
        } },
        .character = CharacterType::Rogue,
    };
    const std::vector<std::string> serialized =
        Level::serializeDefinition(eastDefinition);
    CHECK(Level::parseDefinition(serialized, "minecart round trip") ==
        eastDefinition);

    const Level east = Level::loadFromDefinition(eastDefinition, "east route");
    CHECK(east.minecarts().size() == 1);
    CHECK(east.minecartRoutes()[0].stops ==
        (std::vector<GridPosition3> {
            { 2, 0, 1 }, { 4, 0, 1 },
        }));
    CHECK(!east.minecartRoutes()[0].loop);

    Level::Definition westDefinition = eastDefinition;
    westDefinition.minecarts[0].initialDirection = 3;
    const Level west = Level::loadFromDefinition(westDefinition, "west route");
    CHECK(west.minecartRoutes()[0].stops ==
        (std::vector<GridPosition3> {
            { 2, 0, 1 }, { 0, 0, 1 },
        }));

    const Level loop = Level::loadFromDefinition({
        .layers = {
            { "....", "....", "...." },
            { "6M7C", "| !P", "5_8." },
        },
        .plates = {
            { .cell = { 1, 0, 1 }, .tile = TileType::RailStopEastWest },
        },
        .minecarts = { Level::Minecart {
            .cell = { 1, 0, 1 },
            .pressurePlates = { { 3, 1, 1 } },
            .initialDirection = 1,
        } },
        .character = CharacterType::Rogue,
    }, "loop route");
    CHECK(loop.minecartRoutes()[0].loop);
    CHECK(loop.minecartRoutes()[0].stops ==
        (std::vector<GridPosition3> {
            { 1, 0, 1 }, { 2, 1, 1 }, { 1, 2, 1 },
        }));

    Level::Definition missingStop = eastDefinition;
    missingStop.plates.clear();
    checkThrowsContaining([&] {
        (void)Level::loadFromDefinition(missingStop, "missing stop");
    }, "on top of a Rail Stop");

    Level::Definition wrongDirection = eastDefinition;
    wrongDirection.minecarts[0].initialDirection = 0;
    checkThrowsContaining([&] {
        (void)Level::loadFromDefinition(wrongDirection, "wrong direction");
    }, "must follow its starting Rail Stop");
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

void testDecorativeTileIsSerializedAndGameplayTransparent()
{
    TEST("decorativeTileIsSerializedAndGameplayTransparent");
    CHECK(tileTypeToChar(TileType::Decorative) == 'D');
    CHECK(charToTileType('D') == TileType::Decorative);
    CHECK(tileTypeName(TileType::Decorative) == "Decorative Block");
    CHECK(tileTypeAllowsEntity(TileType::Decorative));
    CHECK(!tileTypeIsSolidBlock(TileType::Decorative));
    CHECK(!tileTypeSupportsEntity(TileType::Decorative));
    CHECK(!tileTypeOccupiesLevelCell(TileType::Decorative));
    CHECK(!tileTypeAffectsCameraFit(TileType::Decorative));

    const Level::LayerRows layers {
        { "..." },
        { "CD " },
    };
    const Level level = Level::loadFromLayers(layers, "decorative tile");
    CHECK(level.tileAt(1, 0, 1) == TileType::Decorative);
    CHECK(level.isWalkable({ 1, 0, 1 }));
    CHECK(Level::parseLayerRows(
        Level::serializeLayerRows(layers), "decorative round trip") == layers);
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

void testPortalMetadata()
{
    TEST("portalMetadataRoundTripsAndValidatesCells");
    const Level::Definition definition {
        .layers = { { ".....", "....." }, { "CO   ", "   O " } },
        .portals = { { .cell = { 1, 0, 1 }, .color = { 0.2f, 0.4f, 1.0f } },
                     { .cell = { 3, 1, 1 }, .color = { 0.2f, 0.4f, 1.0f } } },
    };
    const auto lines = Level::serializeDefinition(definition);
    const auto parsed = Level::parseDefinition(lines, "portal metadata");
    CHECK(parsed == definition);
    CHECK(
        Level::loadFromDefinition(parsed, "portals").portalExit({ 1, 0, 1 }) ==
        GridPosition3({ 3, 1, 1 }));
    auto invalid = definition;
    invalid.portals[0].cell = { 0, 0, 1 };
    checkThrowsContaining(
        [&] { (void)Level::loadFromDefinition(invalid, "bad cell"); },
        "Portal metadata cell");
    invalid = definition;
    invalid.portals.push_back(invalid.portals.front());
    checkThrowsContaining(
        [&] { (void)Level::loadFromDefinition(invalid, "duplicate"); },
        "same cell");
    invalid = definition;
    invalid.portals[0].color.x = 2.0f;
    checkThrowsContaining(
        [&] { (void)Level::loadFromDefinition(invalid, "invalid color"); },
        "zero to one");
    CHECK(tileTypeIsPlate(TileType::PortalNorth));
    CHECK(tileTypeAllowsEntity(TileType::PortalNorth));
    CHECK(!tileTypeIsMovableObject(TileType::PortalNorth));
}


void testLockPlateMetadata()
{
    TEST("lockPlateMetadata");
    const Level::Definition definition {
        .layers = { { "...." }, { "CPRJ" } },
        .lockPlates = {
            { .cell = { 2, 0, 1 }, .pressurePlates = { { 1, 0, 1 } }, .startEnabled = true },
            { .cell = { 3, 0, 1 } },
        },
        .plates = { { .cell = { 2, 0, 1 }, .tile = TileType::LockPlate } },
    };
    const auto parsed = Level::parseDefinition(Level::serializeDefinition(definition), "lock roundtrip");
    CHECK(parsed == definition);
    const auto level = Level::loadFromDefinition(parsed, "lock");
    CHECK(level.plateAt({ 2, 0, 1 }) == TileType::LockPlate);
    CHECK(level.lockPlateForPressurePlate({ 1, 0, 1 }) == level.lockPlateAt({ 2, 0, 1 }));
    CHECK(level.pressurePlateLinkColor({ 1, 0, 1 }) == definition.lockPlates[0].color);
    CHECK(tileTypeFromName("Lock Plate") == TileType::LockPlate);
    auto invalid = definition;
    invalid.lockPlates.clear();
    checkThrowsContaining([&] { (void)Level::loadFromDefinition(invalid, "missing"); }, "@lockplate");
    invalid = definition;
    invalid.lockPlates[0].pressurePlates = { { 0, 0, 1 } };
    checkThrowsContaining([&] { (void)Level::loadFromDefinition(invalid, "bad link"); }, "Pressure or Button tiles");
    checkThrowsContaining([&] { (void)Level::parseDefinition({
        "@lockplate {\"cell\":[0,0,1],\"plates\":[],\"color\":[1,1,1],\"startEnabled\":1}",
        "@layer 0", "..", "@layer 1", "CJ" }, "bad toggle"); }, "startEnabled");
}

void testMinecartGatePreservesRailsAndRoutes()
{
    TEST("minecartGatePreservesRailsAndRoutes");
    CHECK(charToTileType('g') == TileType::MinecartGate);
    CHECK(!tileTypeAllowsEntity(TileType::MinecartGate));
    CHECK(!tileTypeSupportsEntity(TileType::MinecartGate));
    for (TileType rail : { TileType::RailStraightNorthSouth,
             TileType::RailStraightEastWest, TileType::RailCornerNorthEast,
             TileType::RailCornerSouthEast, TileType::RailCornerSouthWest,
             TileType::RailCornerNorthWest, TileType::RailStopNorthSouth,
             TileType::RailStopEastWest }) {
        const Level::Definition definition {
            .layers = { { ".." }, { "Cg" } },
            .plates = { { .cell = { 1, 0, 1 }, .tile = rail } },
        };
        const auto text = Level::serializeDefinition(definition);
        const auto parsed = Level::parseDefinition(text, "gate rail round trip");
        const Level level = Level::loadFromDefinition(parsed, "gate rail");
        CHECK(level.tileAt(1, 0, 1) == TileType::MinecartGate);
        CHECK(level.plateAt({ 1, 0, 1 }) == rail);
        CHECK(Level::serializeDefinition(parsed) == text);
    }
    const Level loop = Level::loadFromDefinition({
        .layers = { { "....", "....", "...." },
                    { "6MgC", "| !P", "5_8." } },
        .plates = {
            { .cell = { 1, 0, 1 }, .tile = TileType::RailStopEastWest },
            { .cell = { 2, 0, 1 }, .tile = TileType::RailCornerSouthWest },
        },
        .minecarts = { { .cell = { 1, 0, 1 },
            .pressurePlates = { { 3, 1, 1 } }, .initialDirection = 1 } },
    }, "gate over corner");
    CHECK(loop.minecartRoutes()[0].loop);
    CHECK(std::ranges::find(loop.minecartRoutes()[0].cells, GridPosition3 { 2, 0, 1 }) !=
        loop.minecartRoutes()[0].cells.end());
    checkThrowsContaining([] {
        (void)Level::loadFromDefinition({
            .layers = { { ".." }, { "Cg" } },
            .plates = { { .cell = { 1, 0, 1 }, .tile = TileType::PressurePlate } },
        }, "gate on wrong surface");
    }, "compatible with its surface");
}

void testGroundSplatMetadata()
{
    TEST("groundSplatMetadata");
    Level::Definition definition {
        .layers = { { "..." }, { "C  " } },
        .groundSplats = {
            { "Meadow", "Grass", "Stone", "MeadowMask", { 0, 1, 0 } },
            { "Sand", "Sand", "Mud", "SandMask", { 1, 0, 0 } },
        },
        .groundPaint = { { { 2, 0, 0 }, "Sand" } },
    };
    const auto lines = Level::serializeDefinition(definition);
    const auto parsed = Level::parseDefinition(lines, "ground round trip");
    CHECK(parsed.groundSplats == definition.groundSplats);
    CHECK(parsed.groundPaint == definition.groundPaint);
    const auto level = Level::loadFromDefinition(parsed, "ground runtime");
    CHECK(level.groundSplats() == definition.groundSplats);
    CHECK(Level::groundSplatAt(level.groundSplats(), level.groundPaint(), { 0, 0, 0 })->name == "Meadow");
    CHECK(Level::groundSplatAt(level.groundSplats(), level.groundPaint(), { 2, 0, 0 })->name == "Sand");
    CHECK(Level::groundSplatAt(level.groundSplats(), level.groundPaint(), { 2, 0, 1 })->name == "Meadow");
    checkThrowsContaining([&] { auto bad = definition; bad.groundPaint[0].splat = "Missing";
        (void)Level::serializeDefinition(bad); }, "unknown splat");
    checkThrowsContaining([&] { auto bad = definition; bad.groundSplats[1].color = { 0, 1, 0 };
        (void)Level::loadFromDefinition(bad, "duplicate colors"); }, "colors must be unique");
    checkThrowsContaining([&] { auto bad = definition; bad.groundSplats[1].name = "Meadow";
        (void)Level::serializeDefinition(bad); }, "names must be unique");
    checkThrowsContaining([&] { auto bad = definition; bad.groundPaint.push_back(bad.groundPaint[0]);
        (void)Level::serializeDefinition(bad); }, "Duplicate ground paint");
    checkThrowsContaining([&] { auto bad = definition; bad.groundPaint[0].cell.z = 1;
        (void)Level::serializeDefinition(bad); }, "ground tile inside");
    checkThrowsContaining([&] { auto bad = definition; bad.groundSplats[0].color.x = -1;
        (void)Level::serializeDefinition(bad); }, "finite and in");
    checkThrowsContaining([&] { auto bad = definition; bad.groundSplats[0].mask.clear();
        (void)Level::serializeDefinition(bad); }, "must not be empty");
    CHECK(Level::parseDefinition({ "C." }, "legacy ground").groundSplats.empty());
}

} // namespace

int main()
{
    TEST("lecternMetadata");
    const Level::Definition lecternDefinition {
        .layers = { { "..." }, { "CT " } },
        .lecterns = { { .cell = { 1, 0, 1 }, .text = "Use \"arrows\".\n\nPush the rock.\\" } },
    };
    const auto lecternLines = Level::serializeDefinition(lecternDefinition);
    CHECK(Level::parseDefinition(lecternLines, "lectern").lecterns == lecternDefinition.lecterns);
    const Level lecternLevel = Level::loadFromLines(lecternLines, "lectern");
    CHECK(lecternLevel.lecternAt({ 1, 0, 1 }) != nullptr);
    CHECK(lecternLevel.lecternAt({ 1, 0, 1 })->text == lecternDefinition.lecterns[0].text);
    CHECK(!lecternLevel.isWalkable({ 1, 0, 1 }));
    CHECK(!lecternLevel.lecternAt({ 0, 0, 1 }));
    checkThrowsContaining([&] {
        auto bad = lecternDefinition;
        bad.lecterns.push_back(bad.lecterns[0]);
        (void)Level::loadFromDefinition(bad, "duplicate");
    }, "Duplicate lectern");
    checkThrowsContaining([&] {
        auto bad = lecternDefinition;
        bad.lecterns[0].cell = { 0, 0, 1 };
        (void)Level::loadFromDefinition(bad, "wrong tile");
    }, "must reference");
    checkThrowsContaining([] {
        (void)Level::parseDefinition({ "@lectern {\"cell\":[1,0,1],\"text\":3}", "@layer 0", "CT" }, "bad text");
    }, "Invalid lectern");
    CHECK(Level::loadFromLayers({ { ".." }, { "CT" } }, "blank book").lecternAt({ 1, 0, 1 })->text.empty());
    CHECK(lecternLevel.tileAt(1, 0, 1) == TileType::LecternSouth);
    CHECK(Level::loadFromLines({
        "@lectern {\"cell\":[1,0,1],\"text\":\"Legacy book\"}",
        "@layer 0", "..", "@layer 1", "CT",
    }, "legacy book").tileAt(1, 0, 1) == TileType::LecternSouth);
    constexpr std::array lecternTiles { TileType::LecternNorth, TileType::LecternEast,
        TileType::LecternSouth, TileType::LecternWest };
    for (std::size_t direction = 0; direction < lecternTiles.size(); ++direction) {
        auto rotated = lecternDefinition;
        rotated.layers[1][0][1] = tileTypeToChar(lecternTiles[direction]);
        const auto lines = Level::serializeDefinition(rotated);
        CHECK(Level::parseDefinition(lines, "rotated book").lecterns == rotated.lecterns);
        const auto level = Level::loadFromLines(lines, "rotated book");
        CHECK(level.tileAt(1, 0, 1) == lecternTiles[direction]);
        CHECK(level.lecternAt({ 1, 0, 1 })->text == rotated.lecterns[0].text);
        CHECK(!level.isWalkable({ 1, 0, 1 }));
        CHECK(Level::loadFromLines({
            "@lectern {\"cell\":[1,0,1],\"text\":\"Book\",\"direction\":" + std::to_string(direction) + "}",
            "@layer 0", "..", "@layer 1", "CT",
        }, "metadata direction").tileAt(1, 0, 1) == lecternTiles[direction]);
    }
    for (const std::string value : { "-1", "4", "256", "4294967296", "18446744073709551615",
             "1.5", "true", "null", "\"north\"" }) {
        checkThrowsContaining([&] {
            (void)Level::parseDefinition({
                "@lectern {\"cell\":[1,0,1],\"text\":\"Book\",\"direction\":" + value + "}",
                "@layer 0", "CT",
            }, "bad direction");
        }, "direction");
    }
    CHECK(tileTypeFromName("Lectern") == TileType::LecternSouth);
    testLockPlateMetadata();
    testPortalMetadata();
    testGroundSplatMetadata();
    testLegacyAndLayeredParsing();
    testSerializationRoundTrip();
    testCameraMetadataRoundTripAndValidation();
    testCharacterMetadataRoundTripAndLegacyDefault();
    testWardrobeVariantsRoundTripAndRemainTraversable();
    testStoneWallVariantsRoundTripAndSupportUnits();
    testWaterLayerMetadataAndTileResolution();
    testDecorationMetadataRoundTrip();
    testSelectorMetadataRoundTripAndLookup();
    testGateMetadataRoundTripAndValidation();
    testRotatorMetadataRoundTripAndValidation();
    testElevatorMetadataRoundTripAndValidation();
    testEditorLinkColorsRoundTripAndDoNotAffectGameplay();
    testGateStartOpenRoundTrips();
    testObjectLinksRoundTripAndValidateMovableCells();
    testPlatePropertyAndCoveredPlateRecords();
    testParserRejectsMalformedStructure();
    testLevelValidationErrors();
    testMinecartMetadataRoutesAndValidation();
    testMinecartGatePreservesRailsAndRoutes();
    testRaggedLayersNormalizeToAir();
    testDecorativeTileIsSerializedAndGameplayTransparent();
    testLadderRequiresSameLayerGround();
    testFileLoadingHandlesCrLfAndMissingFiles();

    if (failures == 0) {
        std::cout << "LevelTests: " << checks << " checks passed\n";
        return 0;
    }

    std::cerr << "LevelTests: " << failures << " of " << checks << " checks failed\n";
    return 1;
}
