// Headless tests for the tile thumbnail bake's scene and crop.
//
// The bake renders a tile through the real frame path and screenshots it, so
// the part worth pinning down here is the part that decides *what* is on
// screen and *where* the crop lands - the rest is the game's own renderer.

#include "engine/AssetManifest.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/TileThumbnailBake.hpp"
#include "engine/render/IsoScenePreparer.hpp"

#include <cmath>
#include <iostream>
#include <set>
#include <string>

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
        std::cerr << "FAIL [" << currentTest << "] line "
                  << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)
#define TEST(name) currentTest = name

const AssetManifest& testManifest()
{
    static const AssetManifest manifest = AssetManifest::parse(R"json({
      "format": 1,
      "textures": [
        { "name": "GroundGrass", "path": "grass.png" },
        { "name": "GroundRock", "path": "rock.png" },
        { "name": "GroundSplatMap", "path": "splat.png" }
      ],
      "models": [
        { "name": "Bricks", "path": "bricks.gltf" },
        { "name": "Hero", "path": "h.glb", "geometry": "skinned", "role": "player" }
      ],
      "animations": [
        { "name": "Idle", "path": "a.glb", "role": "player-idle" },
        { "name": "Move", "path": "a.glb", "role": "player-move" },
        { "name": "Push", "path": "a.glb", "role": "player-push" },
        { "name": "Death", "path": "a.glb", "role": "player-death" },
        { "name": "DeadIdle", "path": "a.glb", "role": "player-dead-idle" }
      ],
      "tiles": [
        { "tile": "Wall", "model": "Bricks" },
        { "tile": "Player", "model": "Hero" }
      ]
    })json");
    return manifest;
}

void testAssetPathsAreUniqueAndTidy()
{
    TEST("assetPathsAreUniqueAndTidy");
    std::set<std::string> paths;
    for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
        if (!tileThumbnails::shouldBake(definition.type)) {
            continue;
        }
        const std::string path = tileThumbnails::assetPathFor(definition.type);
        // Two tiles sharing a path would silently overwrite each other during
        // the bake, and the palette would show one of them twice.
        CHECK(paths.insert(path).second);
        CHECK(path.starts_with("custom/thumbnails/tile_"));
        CHECK(path.ends_with(".png"));
        // Filesystem-safe: no spaces, capitals or punctuation from the display
        // name survive into the file name.
        for (const char character : path) {
            const bool allowed = (character >= 'a' && character <= 'z') ||
                (character >= '0' && character <= '9') ||
                character == '_' || character == '/' || character == '.';
            CHECK(allowed);
        }
    }
    CHECK(paths.size() > 10);

    // Names come from the display name, so multi-word tiles read sensibly.
    CHECK(tileThumbnails::assetPathFor(TileType::ConveyorUp) ==
        "custom/thumbnails/tile_conveyor_up.png");
    CHECK(tileThumbnails::assetPathFor(TileType::MirrorNorthWest) ==
        "custom/thumbnails/tile_mirror_north_west.png");
}

void testAirAndWaterAreNotBaked()
{
    TEST("airAndWaterAreNotBaked");
    // Air is the eraser and Water is a layer property, so neither appears in
    // the palette as a drawable brush.
    CHECK(!tileThumbnails::shouldBake(TileType::Air));
    CHECK(!tileThumbnails::shouldBake(TileType::Water));
    CHECK(tileThumbnails::shouldBake(TileType::Wall));
    CHECK(tileThumbnails::shouldBake(TileType::Ground));
    CHECK(tileThumbnails::shouldBake(TileType::Player));
}

// The bake takes the game's live settings. Shadows and ambient occlusion are
// off in the RenderFrameData defaults, so these are switched on here to prove
// they reach the frame rather than being silently dropped.
[[nodiscard]] const PresentationSettings& testSettings()
{
    static const PresentationSettings settings = [] {
        PresentationSettings value;
        value.applyTileScales(testManifest());
        value.lighting.shadowsEnabled = true;
        value.lighting.shadowOpacity = 0.6f;
        value.lighting.ambientOcclusionEnabled = true;
        value.lighting.ambientOcclusionStrength = 0.5f;
        value.normalize();
        return value;
    }();
    return settings;
}

void testBakeFrameStandsTheTileOnAGroundBed()
{
    TEST("bakeFrameStandsTheTileOnAGroundBed");
    for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
        if (!tileThumbnails::shouldBake(definition.type)) {
            continue;
        }
        const RenderFrameData frame = tileThumbnails::buildBakeFrame(
            definition.type, testManifest(), testSettings());
        CHECK(frame.viewMode == RenderViewMode::Isometric3D);

        // A bed of neutral ground plus the subject. Ground replaces its centre
        // cell rather than stacking on it, so it is one fewer.
        const std::size_t bedCells =
            tileThumbnails::bedSize * tileThumbnails::bedSize;
        const std::size_t expected =
            definition.type == TileType::Ground ? bedCells : bedCells + 1;
        CHECK(frame.tiles.size() == expected);

        // Every tile counts toward the camera fit. This is what makes the
        // camera identical for every thumbnail: fitting to the subject alone
        // framed a flat tile and a tall one at completely different scales.
        for (const RenderFrameData::Tile& tile : frame.tiles) {
            CHECK(tile.affectsCameraFit);
            CHECK(!tile.showGrid);
            CHECK(!tile.isEditorPreview);
        }

        // Lighting is carried through, or the capture would have no shadows
        // and no ambient occlusion - the whole reason for the bed.
        CHECK(frame.lighting.shadows.enabled);
        CHECK(frame.lighting.ambientOcclusion.enabled);

        // The subject is the last tile and is centred on the centre cell. Its
        // footprint is checked by its midpoint rather than its corner because
        // a manifest tile scale above 1 legitimately overhangs the cell.
        const RenderFrameData::Tile& subject = frame.tiles.back();
        const auto centre = static_cast<float>(tileThumbnails::bedCentre);
        const float midX = subject.position.x + subject.size.x * 0.5f;
        const float midY = subject.position.y + subject.size.y * 0.5f;
        CHECK(std::abs(midX - (centre + 0.5f)) < 0.001f);
        CHECK(std::abs(midY - (centre + 0.5f)) < 0.001f);
        // Standing on the bed's surface, not sunk into or floating above it.
        CHECK(subject.baseElevation == 0.0f);
    }
}

void testBedIsNeutralAndFlat()
{
    TEST("bedIsNeutralAndFlat");
    const RenderFrameData frame = tileThumbnails::buildBakeFrame(
        TileType::Wall, testManifest(), testSettings());
    const std::size_t bedCells =
        tileThumbnails::bedSize * tileThumbnails::bedSize;
    for (std::size_t i = 0; i < bedCells; ++i) {
        const RenderFrameData::Tile& cell = frame.tiles[i];
        CHECK(cell.height == 0.0f);
        // Plain cubes, not Ground: a screen's splat map must not be able to
        // change what the thumbnails look like.
        CHECK(cell.effect == RenderSurfaceEffect::Standard);
        CHECK(cell.model.isCube());
        CHECK(cell.color.x == tileThumbnails::bedColor.x);
    }
}

void testGroundIsBakedThroughTheSplatPath()
{
    TEST("groundIsBakedThroughTheSplatPath");
    // Ground's look comes from the splat shader rather than a model, so the
    // bake has to request that path and supply the textures - otherwise the
    // thumbnail would be a flat untextured square.
    const RenderFrameData ground =
        tileThumbnails::buildBakeFrame(
            TileType::Ground, testManifest(), testSettings());
    CHECK(ground.tiles.back().effect == RenderSurfaceEffect::GroundSplat);
    CHECK(ground.groundSplat.valid());

    const RenderFrameData wall =
        tileThumbnails::buildBakeFrame(
            TileType::Wall, testManifest(), testSettings());
    CHECK(wall.tiles.back().effect == RenderSurfaceEffect::Standard);
    CHECK(!wall.tiles.back().model.isCube());
}

void testMirrorsBakeAtTheirOwnOrientation()
{
    TEST("mirrorsBakeAtTheirOwnOrientation");
    // The four mirrors share one model and differ only by rotation, so
    // baking them all unrotated would produce four identical pictures.
    std::set<uint32_t> turns;
    for (const TileType mirror : {
             TileType::MirrorNorthWest, TileType::MirrorNorthEast,
             TileType::MirrorSouthWest, TileType::MirrorSouthEast }) {
        const RenderFrameData frame =
            tileThumbnails::buildBakeFrame(
                mirror, testManifest(), testSettings());
        turns.insert(frame.tiles.back().modelRotationQuarterTurns);
        CHECK(frame.tiles.back().modelRotationOffsetRadians != 0.0f);
    }
    CHECK(turns.size() == 4);
}

void testConveyorsBakeRotatedAndAtBeltHeight()
{
    TEST("conveyorsBakeRotatedAndAtBeltHeight");
    // Regression: the bake used to re-derive the tile's height and rotation
    // and handled only mirrors, so all four conveyors baked identically - and
    // flat, because a conveyor is neither a surface entity nor a solid block.
    std::set<uint32_t> turns;
    for (const TileType conveyor : {
             TileType::ConveyorUp, TileType::ConveyorDown,
             TileType::ConveyorLeft, TileType::ConveyorRight }) {
        const RenderFrameData frame = tileThumbnails::buildBakeFrame(
            conveyor, testManifest(), testSettings());
        const RenderFrameData::Tile& subject = frame.tiles.back();
        turns.insert(subject.modelRotationQuarterTurns);
        // A belt, not a floor decal and not a full cube.
        CHECK(subject.height > 0.0f);
        CHECK(subject.height < 1.0f);
    }
    CHECK(turns.size() == 4);
}

void testSubjectMatchesTheTileTheEditorDraws()
{
    TEST("subjectMatchesTheTileTheEditorDraws");
    // The bake and the editor must agree on what a tile looks like, since the
    // palette icon is meant to be a picture of the tile the editor will place.
    // Both go through tileVisual, so this pins the bake to it and would catch
    // the bake growing its own copy of the rules again.
    for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
        if (!tileThumbnails::shouldBake(definition.type)) {
            continue;
        }
        const RenderFrameData frame = tileThumbnails::buildBakeFrame(
            definition.type, testManifest(), testSettings());
        const RenderFrameData::Tile& subject = frame.tiles.back();
        const RenderFrameData::Tile expected = tileVisual(
            definition.type,
            {
                static_cast<int>(tileThumbnails::bedCentre),
                static_cast<int>(tileThumbnails::bedCentre),
                definition.type == TileType::Ground ? 0 : 1,
            },
            testManifest(),
            testSettings());
        CHECK(subject.height == expected.height);
        CHECK(subject.size.x == expected.size.x);
        CHECK(subject.size.y == expected.size.y);
        CHECK(subject.position.x == expected.position.x);
        CHECK(subject.position.y == expected.position.y);
        CHECK(subject.modelRotationQuarterTurns ==
            expected.modelRotationQuarterTurns);
        CHECK(subject.modelRotationOffsetRadians ==
            expected.modelRotationOffsetRadians);
        CHECK(subject.effect == expected.effect);
        CHECK(subject.color.x == expected.color.x);
        CHECK(subject.color.w == expected.color.w);
    }
}

void testPerspectiveIsNoStrongerThanOnARealBoard()
{
    TEST("perspectiveIsNoStrongerThanOnARealBoard");
    // Camera distance follows the size of the framed area, so fitting to a 3x3
    // bed put the camera very close and a tile's vertical edges visibly
    // splayed. This measures that splay directly rather than asserting the
    // multiplier's value: a vertical world edge is exactly vertical on screen
    // under an orthographic iso view, so any horizontal drift between a
    // corner's base and its top is the perspective divergence.
    constexpr uint32_t width = 1280;
    constexpr uint32_t height = 720;

    const auto leanFraction = [](const RenderFrameData& frame) {
        PreparedRenderScene scene;
        const IsoScenePreparer preparer;
        preparer.prepare(frame,
            { static_cast<float>(width), static_cast<float>(height) }, scene);
        const auto pixelX = [&](Vec3 point) {
            return (IsoScenePreparer::projectIsoPoint(
                        scene.isoLayout, scene.renderExtent, point)
                           .x +
                       1.0f) *
                0.5f * static_cast<float>(width);
        };
        const auto centre = static_cast<float>(tileThumbnails::bedCentre);
        float lean = 0.0f;
        for (const float x : { centre, centre + 1.0f }) {
            for (const float y : { centre, centre + 1.0f }) {
                lean = std::max(lean,
                    std::abs(pixelX({ x, y, 1.0f }) - pixelX({ x, y, 0.0f })));
            }
        }
        const tileThumbnails::CropRect crop =
            tileThumbnails::cropFor(frame, width, height);
        return lean / static_cast<float>(std::max(crop.width, 1u));
    };

    const RenderFrameData frame = tileThumbnails::buildBakeFrame(
        TileType::Wall, testManifest(), testSettings());
    // A 9-wide board - a small level - produces about 1.2%, so this is the
    // loosest bound that still means "no worse than the game".
    CHECK(leanFraction(frame) < 0.012f);

    // The bed alone, with the ordinary fitted distance, is well past that.
    // Without this the bound above could be met by accident.
    RenderFrameData close = frame;
    close.cameraDistanceMultiplier.reset();
    CHECK(leanFraction(close) > 0.03f);

    // Pulling the camera back must not shrink the subject: the fit rescales to
    // compensate, which is what makes this a lens choice and not a zoom.
    const tileThumbnails::CropRect crop =
        tileThumbnails::cropFor(frame, width, height);
    const tileThumbnails::CropRect closeCrop =
        tileThumbnails::cropFor(close, width, height);
    CHECK(crop.width >= closeCrop.width);
}

void testCropFramesTheSubjectCell()
{
    TEST("cropFramesTheSubjectCell");
    const std::pair<uint32_t, uint32_t> extents[] = {
        { 1280, 720 }, { 720, 1280 }, { 800, 800 }, { 1920, 1080 }, { 64, 64 },
    };
    const RenderFrameData frame = tileThumbnails::buildBakeFrame(
        TileType::Wall, testManifest(), testSettings());
    for (const auto& [width, height] : extents) {
        const tileThumbnails::CropRect crop =
            tileThumbnails::cropFor(frame, width, height);
        CHECK(crop.width > 0);
        CHECK(crop.height > 0);
        // Square, so the saved thumbnail is not stretched by the window's
        // aspect ratio.
        CHECK(crop.width == crop.height);
        // Entirely inside the render extent, or the capture would read
        // outside the image.
        CHECK(crop.x >= 0);
        CHECK(crop.y >= 0);
        CHECK(static_cast<uint32_t>(crop.x) + crop.width <= width);
        CHECK(static_cast<uint32_t>(crop.y) + crop.height <= height);
        // The crop must actually contain the subject cell's projection, which
        // is the point of deriving it rather than guessing a fraction.
        PreparedRenderScene scene;
        const IsoScenePreparer preparer;
        preparer.prepare(
            frame,
            { static_cast<float>(width), static_cast<float>(height) },
            scene);
        const auto centre = static_cast<float>(tileThumbnails::bedCentre);
        const Vec3 clip = IsoScenePreparer::projectIsoPoint(
            scene.isoLayout, scene.renderExtent,
            { centre + 0.5f, centre + 0.5f, 0.0f });
        const float pixelX = (clip.x + 1.0f) * 0.5f * static_cast<float>(width);
        const float pixelY =
            (1.0f - clip.y) * 0.5f * static_cast<float>(height);
        CHECK(pixelX >= static_cast<float>(crop.x));
        CHECK(pixelX <= static_cast<float>(crop.x + static_cast<int32_t>(crop.width)));
        CHECK(pixelY >= static_cast<float>(crop.y));
        CHECK(pixelY <= static_cast<float>(crop.y + static_cast<int32_t>(crop.height)));

        // Smaller than the whole extent, or the bed's outer cells would fill
        // the picture and the subject would be a speck.
        CHECK(crop.width < std::min(width, height) ||
            std::min(width, height) <= 64);
    }

    // Degenerate extents must not produce a zero or negative rectangle.
    const tileThumbnails::CropRect tiny = tileThumbnails::cropFor(frame, 1, 1);
    CHECK(tiny.width >= 1);
    CHECK(tiny.height >= 1);
    CHECK(tiny.x == 0);
    CHECK(tiny.y == 0);
}

} // namespace

int main()
{
    testAssetPathsAreUniqueAndTidy();
    testAirAndWaterAreNotBaked();
    testBakeFrameStandsTheTileOnAGroundBed();
    testBedIsNeutralAndFlat();
    testGroundIsBakedThroughTheSplatPath();
    testMirrorsBakeAtTheirOwnOrientation();
    testConveyorsBakeRotatedAndAtBeltHeight();
    testSubjectMatchesTheTileTheEditorDraws();
    testPerspectiveIsNoStrongerThanOnARealBoard();
    testCropFramesTheSubjectCell();

    if (failures == 0) {
        std::cout << "TileThumbnailBakeTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "TileThumbnailBakeTests: "
              << failures << " of " << checks << " checks failed\n";
    return 1;
}
