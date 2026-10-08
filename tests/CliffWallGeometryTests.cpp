#include "TestHarness.hpp"

#include "engine/CliffWallGeometry.hpp"
#include "engine/GroundGeometry.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/render/GroundChunkGeometry.hpp"
#include "engine/render/GroundRimSurface.hpp"
#include "engine/render/IsoScenePreparer.hpp"
#include "engine/render/RenderAssetRequirements.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace sokoban;

constexpr std::array<uint8_t, 6> canonicalMasks { 15, 11, 5, 3, 1, 0 };
constexpr std::array<GridPosition, 4> offsets {{ { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } }};
constexpr std::array<std::array<float, 13>, 2> expectedTopInsets {{
    { 0, 0, .0429f, .066f, .05808f, .07788f, .06864f, .0594f, .07392f, .05478f, .03762f, 0, 0 },
    { 0, 0, .05412f, .04026f, .07128f, .05808f, .07986f, .06468f, .05544f, .06864f, .04884f, 0, 0 },
}};
constexpr std::array<float, 2> expectedCornerTaper { .0605f, .066f };
constexpr std::array<uint8_t, 4> convexCornerMasks { 9, 3, 6, 12 };
constexpr std::array<Vec2, 4> unitCorners {{ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }};

float twiceArea(Vec2 a, Vec2 b, Vec2 c)
{
    return (b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x);
}

AssetManifest makeManifest(std::string_view omitted = {}, bool omitIslands = false)
{
    std::string json = R"({"format":1,"models":[)";
    bool first = true;
    for (const auto& shape : cliffWallModelNames) {
        for (std::string_view name : shape) {
            if (name == omitted || (omitIslands && name.starts_with("CliffWallIsland"))) continue;
            if (!first) json += ',';
            first = false;
            json += "{\"name\":\"" + std::string(name) + "\",\"path\":\"mock/" +
                std::string(name) + ".glb\",\"preserveSourceScale\":true}";
        }
    }
    if (!first) json += ',';
    json += R"({"name":"GroundRock01","path":"custom/pbr/models/GroundRock01.glb","preserveSourceScale":true},
        {"name":"OtherWall","path":"mock/wall.glb","preserveSourceScale":true},
        {"name":"Hero","path":"mock/hero.glb","geometry":"skinned","role":"player"}],
        "animations":[
            {"name":"Idle","path":"mock/idle.glb","role":"player-idle"},
            {"name":"Move","path":"mock/move.glb","role":"player-move"},
            {"name":"Push","path":"mock/push.glb","role":"player-push"},
            {"name":"Death","path":"mock/death.glb","role":"player-death"},
            {"name":"DeadIdle","path":"mock/dead.glb","role":"player-dead-idle"}],
        "tiles":[])";
    json += '}';
    return AssetManifest::parse(json);
}

RenderFrameData::Tile cliff(GridPosition3 cell, const AssetManifest& manifest, uint32_t variant = 0)
{
    RenderFrameData::Tile tile {
        .cell = cell,
        .position = { static_cast<float>(cell.x), static_cast<float>(cell.y) },
        .color = { 1, 1, 1, 1 },
        .baseElevation = static_cast<float>(cell.z),
        .height = 1,
        .model = manifest.findModelIdByName(cliffWallModelNames[0][variant]).value_or(cubeModel),
        .effect = RenderSurfaceEffect::GroundSplat,
        .groundTop = true,
    };
    tile.cliffWall = true;
    tile.cliffWallVariant = variant;
    return tile;
}

uint8_t rotateMask(uint8_t mask, uint32_t turns)
{
    for (uint32_t index = 0; index < turns % 4; ++index) {
        mask = static_cast<uint8_t>(((mask << 1) | (mask >> 3)) & 15);
    }
    return mask;
}

uint8_t renderedMask(const RenderFrameData::Tile& tile, const AssetManifest& manifest)
{
    for (std::size_t shape = 0; shape < cliffWallModelNames.size(); ++shape) {
        for (std::string_view name : cliffWallModelNames[shape]) {
            if (tile.model == manifest.findModelIdByName(name))
                return rotateMask(canonicalMasks[shape], tile.modelRotationQuarterTurns);
        }
    }
    CHECK_MESSAGE(false, "selected cliff model is not one of the six canonical shapes");
    return 0xff;
}

void checkMask(const RenderFrameData::Tile& tile, const AssetManifest& manifest, uint8_t expected)
{
    CHECK(renderedMask(tile, manifest) == expected);
    CHECK(tile.cliffWallSideMask == expected);
    CHECK(tile.modelRotationQuarterTurns < 4);
    CHECK(tile.groundTop);
    CHECK(!tile.groundGeometryEligible);
    CHECK(tile.groundRimWidth == 0);
    CHECK(tile.groundRimDepth == 0);
}

void testAllMasksAndBothVariants()
{
    TEST("allMasksAndBothVariants");
    const auto manifest = makeManifest();
    for (uint8_t mask = 0; mask < 16; ++mask) {
        const CliffWallModule module = cliffWallModuleForMask(mask);
        CHECK(module.shapeIndex < canonicalMasks.size());
        CHECK(module.quarterTurns < 4);
        CHECK(rotateMask(canonicalMasks[module.shapeIndex], module.quarterTurns) == mask);
        const auto highBits = cliffWallModuleForMask(static_cast<uint8_t>(mask | 0xf0));
        CHECK(highBits.shapeIndex == module.shapeIndex);
        CHECK(highBits.quarterTurns == module.quarterTurns);
        for (uint32_t variant = 0; variant < 2; ++variant) {
            std::vector tiles { cliff({ 2, 3, 4 }, manifest, variant) };
            tiles.front().groundSplat = GroundSplatTextures {
                .base = RenderTexture { 11 }, .detail = RenderTexture { 12 }, .splatMap = RenderTexture { 13 },
            };
            tiles.front().groundSplatOrigin = { -5, 9 };
            const auto original = tiles.front();
            for (std::size_t side = 0; side < offsets.size(); ++side) {
                if ((mask & (1U << side)) == 0)
                    tiles.push_back(cliff({ 2 + offsets[side].x, 3 + offsets[side].y, 4 }, manifest, 1-variant));
            }
            processCliffWallGeometry(tiles, manifest);
            const auto& selected = tiles.front();
            CHECK(selected.model == manifest.findModelIdByName(cliffWallModelNames[module.shapeIndex][variant]));
            CHECK(selected.modelRotationQuarterTurns == module.quarterTurns);
            checkMask(selected, manifest, mask);
            CHECK(selected.cell == original.cell);
            CHECK(selected.position == original.position);
            CHECK(selected.size == original.size);
            CHECK(selected.baseElevation == original.baseElevation);
            CHECK(selected.height == original.height);
            CHECK(selected.color == original.color);
            CHECK(selected.groundSplat == original.groundSplat);
            CHECK(selected.groundSplatOrigin == original.groundSplatOrigin);
            const auto once = tiles;
            processCliffWallGeometry(tiles, manifest);
            CHECK(tiles == once);
        }
    }
}

void testFilledGridStripAndCorner()
{
    TEST("filledGridStripAndCorner");
    const auto manifest = makeManifest();
    std::vector<RenderFrameData::Tile> grid;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) grid.push_back(cliff({ x, y, 0 }, manifest, (x+y) % 2));
    processCliffWallGeometry(grid, manifest);
    const std::array<uint8_t, 9> masks { 9, 1, 3, 8, 0, 2, 12, 4, 6 };
    for (std::size_t index = 0; index < grid.size(); ++index) checkMask(grid[index], manifest, masks[index]);
    CHECK(grid[4].model == manifest.findModelIdByName("CliffWallInteriorA"));
    std::vector line { cliff({ 0, 0, 0 }, manifest), cliff({ 1, 0, 0 }, manifest, 1), cliff({ 2, 0, 0 }, manifest) };
    processCliffWallGeometry(line, manifest);
    checkMask(line[0], manifest, 13); checkMask(line[1], manifest, 5); checkMask(line[2], manifest, 7);
    std::vector corner { cliff({ 0, 0, 0 }, manifest), cliff({ 1, 0, 0 }, manifest, 1), cliff({ 0, 1, 0 }, manifest) };
    processCliffWallGeometry(corner, manifest);
    checkMask(corner[0], manifest, 9); checkMask(corner[1], manifest, 7); checkMask(corner[2], manifest, 14);
    // Deleting one connection must restore the formerly absent side on rebuild.
    corner.erase(corner.begin() + 2);
    processCliffWallGeometry(corner, manifest);
    checkMask(corner[0], manifest, 13);
}

void testLayersUnsupportedNeighborsAndPreviewContext()
{
    TEST("layersUnsupportedNeighborsAndPreviewContext");
    const auto manifest = makeManifest();
    std::vector layers { cliff({ -4, -6, 2 }, manifest), cliff({ -3, -6, 2 }, manifest, 1),
        cliff({ -4, -7, 3 }, manifest), cliff({ -4, -6, 1 }, manifest) };
    processCliffWallGeometry(layers, manifest);
    checkMask(layers[0], manifest, 13); checkMask(layers[1], manifest, 7);
    checkMask(layers[2], manifest, 15); checkMask(layers[3], manifest, 15);
    const auto checkExcluded = [&](RenderFrameData::Tile neighbor) {
        std::array tiles { cliff({ 0, 0, 0 }, manifest), neighbor };
        processCliffWallGeometry(tiles, manifest);
        checkMask(tiles[0], manifest, 15);
    };
    auto neighbor = cliff({ 1, 0, 0 }, manifest);
    neighbor.cliffWall = false; neighbor.model = manifest.modelIdByName("OtherWall");
    const auto unchanged = neighbor;
    std::array ordinary { cliff({ 0, 0, 0 }, manifest), neighbor };
    processCliffWallGeometry(ordinary, manifest);
    CHECK(ordinary[1] == unchanged);
    checkMask(ordinary[0], manifest, 15);
    neighbor.model = manifest.modelIdByName("GroundRock01"); checkExcluded(neighbor);
    neighbor = cliff({ 1, 0, 0 }, manifest); neighbor.size = { .8f, 1 }; checkExcluded(neighbor);
    neighbor = cliff({ 1, 0, 0 }, manifest); neighbor.height = .9f; checkExcluded(neighbor);
    neighbor = cliff({ 1, 0, 0 }, manifest); neighbor.position.x += .1f; checkExcluded(neighbor);
    neighbor = cliff({ 1, 0, 0 }, manifest); neighbor.baseElevation += .1f; checkExcluded(neighbor);
    neighbor = cliff({ 1, 0, 0 }, manifest); neighbor.isEditorPreview = true;
    std::array preview { cliff({ 0, 0, 0 }, manifest), neighbor };
    processCliffWallGeometry(preview, manifest);
    checkMask(preview[0], manifest, 15); // a brush ghost does not cut the authored wall
    checkMask(preview[1], manifest, 7);  // the ghost still previews its authored neighbor join
    preview[1].baseElevation += .02f;
    preview[1].color.w = .5f;
    processCliffWallGeometry(preview, manifest);
    checkMask(preview[0], manifest, 15);
    checkMask(preview[1], manifest, 7); // preview drawing offsets/opacity do not change logical context
    for (const auto& patch : cliffWallTopPatches(preview[1])) {
        for (Vec3 point : patch) checkNear(point.z, 1.02f, "preview cap follows hover elevation", 1e-6f);
    }
}

void testMissingModelsFailToCompleteIsland()
{
    TEST("missingModelsFailToCompleteIsland");
    const auto missing = makeManifest("CliffWallEdgeA");
    std::vector tiles { cliff({ 0, 0, 0 }, missing), cliff({ 1, 0, 0 }, missing),
        cliff({ 0, 1, 0 }, missing), cliff({ -1, 0, 0 }, missing) };
    processCliffWallGeometry(tiles, missing);
    CHECK(tiles[0].model == missing.modelIdByName("CliffWallIslandA"));
    CHECK(tiles[0].modelRotationQuarterTurns == 0);
    CHECK(tiles[0].groundTop);
    CHECK(tiles[0].cliffWallSideMask == groundAllSides);
    const auto noFallback = makeManifest("CliffWallEdgeA", true);
    for (auto& tile : tiles) { tile.model = cubeModel; tile.modelRotationQuarterTurns = 3; }
    processCliffWallGeometry(tiles, noFallback);
    CHECK(tiles[0].model.isCube());
    CHECK(tiles[0].modelRotationQuarterTurns == 0);
    CHECK(tiles[0].groundTop);
    CHECK(tiles[0].cliffWallSideMask == groundAllSides);
}

void testTaperedTopProfilesCoverageAndRotation()
{
    TEST("taperedTopProfilesCoverageAndRotation");
    CHECK(cliffWallTopInsetProfiles == expectedTopInsets);
    const auto manifest = makeManifest();
    for (uint32_t variant = 0; variant < 2; ++variant) {
        float sideSetbackArea = 0;
        for (float inset : expectedTopInsets[variant]) sideSetbackArea += inset / 12.0f;
        for (uint8_t mask = 0; mask < 16; ++mask) {
            const auto boundary = cliffWallTopBoundary(mask, variant);
            CHECK(boundary.size() == 48);
            const auto convexCount = std::ranges::count_if(convexCornerMasks,
                [mask](uint8_t corner) { return (mask & corner) == corner; });
            float area = 0;
            for (std::size_t index = 0; index < boundary.size(); ++index) {
                const Vec2 point = boundary[index];
                CHECK(point.x >= 0 && point.x <= 1 && point.y >= 0 && point.y <= 1);
                const float triangle = twiceArea({ .5f, .5f }, point,
                    boundary[(index+1) % boundary.size()]);
                CHECK(triangle > 1e-6f);
                area += triangle * .5f;
                const auto side = index / 12;
                const auto sample = index % 12;
                const float inset = side == 0 ? point.y : side == 1 ? 1-point.x
                    : side == 2 ? 1-point.y : point.x;
                const uint8_t startCorner = static_cast<uint8_t>((1U << side) | (1U << ((side+3)%4)));
                const uint8_t endCorner = static_cast<uint8_t>((1U << side) | (1U << ((side+1)%4)));
                const bool tapered = (sample <= 1 && (mask & startCorner) == startCorner) ||
                    (sample == 11 && (mask & endCorner) == endCorner);
                if (!tapered) {
                    checkNear(inset, (mask & (1U << side)) ? expectedTopInsets[variant][sample] : 0,
                        "native inset outside convex-corner support", 1e-6f);
                }
            }
            // The corner and its two neighboring samples cut this additional
            // area from the original polygon; the second samples stay fixed.
            const float cornerSetbackArea = expectedCornerTaper[variant] *
                (1.0f/6.0f-(expectedTopInsets[variant][2]+expectedTopInsets[variant][10])*.25f);
            checkNear(area, 1-std::popcount(static_cast<unsigned>(mask))*sideSetbackArea-
                static_cast<float>(convexCount)*cornerSetbackArea,
                "cap covers cell minus side and convex-corner setbacks", 1e-6f);
            const auto rotated = cliffWallTopBoundary(rotateMask(mask, 1), variant);
            for (std::size_t index = 0; index < boundary.size(); ++index) {
                const Vec2 expected { 1-boundary[index].y, boundary[index].x };
                const Vec2 actual = rotated[(index+12) % boundary.size()];
                checkNear(actual.x, expected.x, "quarter-turn cap x", 1e-6f);
                checkNear(actual.y, expected.y, "quarter-turn cap y", 1e-6f);
            }
            auto tile = cliff({ -3, -4, 2 }, manifest, variant);
            tile.cliffWallSideMask = mask;
            const auto patches = cliffWallTopPatches(tile);
            float patchArea = 0;
            CHECK(patches.size() == 24);
            for (const auto& patch : patches) {
                CHECK(patch[0] == Vec3({ -2.5f, -3.5f, 3 }));
                for (Vec3 point : patch) CHECK(point.z == 3);
                for (const auto triangle : { std::array<std::size_t, 3> { 0, 1, 2 },
                         std::array<std::size_t, 3> { 0, 2, 3 } }) {
                    const auto xy = [&](std::size_t i) { return Vec2 { patch[i].x, patch[i].y }; };
                    const float triangleArea = twiceArea(xy(triangle[0]), xy(triangle[1]), xy(triangle[2]));
                    CHECK(triangleArea > 1e-6f);
                    patchArea += triangleArea * .5f;
                }
            }
            checkNear(patchArea, area, "world cap tessellation has no omitted or duplicate area", 2e-6f);
        }
    }
}

void testOnlyConvexCornersTaperThroughNativeHeight()
{
    TEST("onlyConvexCornersTaperThroughNativeHeight");
    for (uint32_t variant = 0; variant < 2; ++variant) {
        const float amount = expectedCornerTaper[variant];
        for (uint8_t mask = 0; mask < 16; ++mask) {
            const auto boundary = cliffWallTopBoundary(mask, variant);
            for (std::size_t corner = 0; corner < unitCorners.size(); ++corner) {
                const Vec2 original = unitCorners[corner];
                const bool exposed = (mask & convexCornerMasks[corner]) == convexCornerMasks[corner];
                const Vec2 inward { original.x == 0 ? 1.0f : -1.0f,
                    original.y == 0 ? 1.0f : -1.0f };
                CHECK(boundary[corner*12] == original + inward*(exposed ? amount : 0.0f));
                CHECK(cliffWallConvexCornerTaper(original, mask, variant, 0) == original);
                CHECK(cliffWallConvexCornerTaper(original, mask, variant, -1) == original);
                CHECK(cliffWallConvexCornerTaper(original, mask, variant, .5f) ==
                    original + inward*(exposed ? amount*.5f : 0.0f));
                CHECK(cliffWallConvexCornerTaper(original, mask, variant, 2) == boundary[corner*12]);
            }
            CHECK(cliffWallConvexCornerTaper({ .5f, .5f }, mask, variant) == Vec2({ .5f, .5f }));
        }
        // Half-radius along one axis gives half the inward corner shift.
        const Vec2 half = cliffWallConvexCornerTaper({ 1.0f/12.0f, 0 }, 9, variant);
        checkNear(half.x, 1.0f/12.0f+amount*.5f, "linear corner support x", 1e-6f);
        checkNear(half.y, amount*.5f, "linear corner support y", 1e-6f);
        CHECK(cliffWallConvexCornerTaper({ 1.0f/6.0f, 0 }, 9, variant) == Vec2({ 1.0f/6.0f, 0 }));
        const Vec2 outward = cliffWallConvexCornerTaper({ -.02f, -.03f }, 9, variant);
        checkNear(outward.x, -.02f+amount, "native outward relief clamps corner distance x", 1e-6f);
        checkNear(outward.y, -.03f+amount, "native outward relief clamps corner distance y", 1e-6f);
    }
}

void testTaperedHiddenEdgesStayCompleteAcrossJunctions()
{
    TEST("taperedHiddenEdgesStayCompleteAcrossJunctions");
    // Exhaust every pair of side masks that can touch, including L/T junctions
    // where their perpendicular exposure and A/B profiles disagree.
    for (std::size_t axis = 0; axis < 2; ++axis) {
        const unsigned firstSide = axis == 0 ? 1 : 2;
        const unsigned secondSide = axis == 0 ? 3 : 0;
        for (uint8_t firstMask = 0; firstMask < 16; ++firstMask) {
            if (firstMask & (1U << firstSide)) continue;
            for (uint8_t secondMask = 0; secondMask < 16; ++secondMask) {
                if (secondMask & (1U << secondSide)) continue;
                for (uint32_t firstVariant = 0; firstVariant < 2; ++firstVariant) {
                    for (uint32_t secondVariant = 0; secondVariant < 2; ++secondVariant) {
                        const auto first = cliffWallTopBoundary(firstMask, firstVariant);
                        const auto second = cliffWallTopBoundary(secondMask, secondVariant);
                        for (std::size_t sample = 0; sample <= 12; ++sample) {
                            const Vec2 a = first[(firstSide*12+sample) % first.size()];
                            const Vec2 b = second[(secondSide*12+12-sample) % second.size()];
                            checkNear(axis == 0 ? a.x : a.y, 1, "first full hidden join plane", 1e-6f);
                            checkNear(axis == 0 ? b.x : b.y, 0, "second full hidden join plane", 1e-6f);
                            checkNear(axis == 0 ? a.y : a.x, axis == 0 ? b.y : b.x,
                                "matching hidden cap join interval", 1e-6f);
                        }
                    }
                }
            }
        }
    }
}

void testTransformsFlatPaintHitsAndDeformationExclusion()
{
    TEST("transformsFlatPaintHitsAndDeformationExclusion");
    const auto manifest = makeManifest();
    constexpr Vec2 extent { 1280, 720 };
    for (uint32_t turn = 0; turn < 4; ++turn) {
        auto tile = cliff({ 2, 2, 3 }, manifest);
        tile.modelRotationQuarterTurns = turn;
        // Ground processing may run after cliff selection, and must leave the
        // authored body outside its CPU/GPU deformation and chunk pipelines.
        std::array tiles { tile };
        processGroundGeometry(tiles, manifest);
        CHECK(!tiles[0].groundGeometryEligible);
        CHECK(!isGroundChunkTileEligible(tiles[0]));
        CHECK(!hasGroundRimSurface(tiles[0]));
        CHECK(tiles[0].model == tile.model);
        CHECK(tiles[0].modelRotationQuarterTurns == turn);
        const auto transform = IsoScenePreparer::modelWorldTransform(tiles[0]);
        const Vec3 center {
            transform[3].x + .5f*(transform[0].x+transform[1].x),
            transform[3].y + .5f*(transform[0].y+transform[1].y),
            transform[3].z + transform[2].z,
        };
        CHECK(center == Vec3({ 2.5f, 2.5f, 4 }));
        const std::array<Vec2, 4> expectedNorth {{ { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } }};
        CHECK(-transform[1].x == expectedNorth[turn].x);
        CHECK(-transform[1].y == expectedNorth[turn].y);
        RenderFrameData frame;
        frame.viewMode = RenderViewMode::Isometric3D;
        frame.levelWidth = frame.levelHeight = 6; frame.levelDepth = 5;
        frame.cameraExtent = { 0, 0, 0, 6, 6, 5 };
        frame.groundSplat = { .base = RenderTexture { 1 }, .detail = RenderTexture { 2 }, .splatMap = RenderTexture { 3 } };
        frame.tiles.push_back(tiles[0]);
        const IsoScenePreparer preparer;
        PreparedRenderScene scene;
        preparer.prepare(frame, extent, scene);
        std::size_t tops = 0;
        for (std::size_t faceIndex = 0; faceIndex < scene.isoFaces.size(); ++faceIndex) {
            const auto& face = scene.isoFaces[faceIndex];
            if (face.material != PreparedSurfaceMaterial::GroundSplat) continue;
            ++tops;
            CHECK(std::ranges::find(scene.opaqueFaceIndices, faceIndex) != scene.opaqueFaceIndices.end());
            CHECK(face.groundRimSurface);
            for (Vec3 point : face.worldVertices) CHECK(point.z == 4);
        }
        CHECK(tops == cliffWallTopPatchCount);
        const Vec3 world { 2.37f, 2.59f, 4 };
        const Vec3 clip = IsoScenePreparer::projectIsoPoint(scene.isoLayout, extent, world);
        const Vec2 pixel { (clip.x+1)*.5f*extent.x, (1-clip.y)*.5f*extent.y };
        const auto hit = preparer.pickGroundPoint(scene, pixel, extent);
        CHECK(hit.has_value());
        if (hit) { checkNear(hit->x, world.x, "paint x", .002f); checkNear(hit->y, world.y, "paint y", .002f); checkNear(hit->z, world.z, "paint height", .002f); }
        const auto requirements = renderAssetRequirementsForFrame(frame);
        CHECK(requirements.contains(tiles[0].model));
        CHECK(requirements.contains(frame.groundSplat.splatMap));
    }
}

void testTaperedCapsShareDrawPaintAndShadowFootprints()
{
    TEST("taperedCapsShareDrawPaintAndShadowFootprints");
    const auto manifest = makeManifest();
    constexpr Vec2 extent { 1280, 720 };
    const IsoScenePreparer preparer;
    for (uint32_t variant = 0; variant < 2; ++variant) {
        for (uint8_t mask = 0; mask < 16; ++mask) {
            auto tile = cliff({ -3, -4, 2 }, manifest, variant);
            const auto module = cliffWallModuleForMask(mask);
            tile.model = manifest.modelIdByName(cliffWallModelNames[module.shapeIndex][variant]);
            tile.modelRotationQuarterTurns = module.quarterTurns;
            tile.cliffWallSideMask = mask;
            tile.groundSplat = GroundSplatTextures {
                .base = RenderTexture { 11 }, .detail = RenderTexture { 12 }, .splatMap = RenderTexture { 13 },
            };
            tile.groundSplatOrigin = { -5, -9 };
            RenderFrameData frame;
            frame.viewMode = RenderViewMode::Isometric3D;
            frame.levelWidth = frame.levelHeight = 6;
            frame.levelDepth = 4;
            frame.cameraExtent = { -4, -5, 0, 3, 3, 4 };
            frame.tiles.push_back(tile);
            PreparedRenderScene scene;
            preparer.prepare(frame, extent, scene);
            const auto patches = cliffWallTopPatches(tile);
            CHECK(scene.shadowFaces.size() == patches.size());
            std::size_t drawableCaps = 0;
            for (std::size_t index = 0; index < scene.isoFaces.size(); ++index) {
                const auto& face = scene.isoFaces[index];
                if (face.material != PreparedSurfaceMaterial::GroundSplat) continue;
                ++drawableCaps;
                CHECK(std::ranges::find(scene.opaqueFaceIndices, index) != scene.opaqueFaceIndices.end());
                CHECK(std::ranges::find(patches, face.worldVertices) != patches.end());
                CHECK(std::ranges::find(scene.shadowFaces, face.worldVertices) != scene.shadowFaces.end());
                CHECK(face.groundRimSurface);
                CHECK(face.groundSplat == tile.groundSplat);
                CHECK(face.groundSplatOrigin == tile.groundSplatOrigin);
                CHECK(face.cell == tile.cell);
                for (Vec3 point : face.worldVertices) CHECK(point.z == 3);
            }
            CHECK(drawableCaps == patches.size());
            const auto pixelAt = [&](Vec3 world) {
                const Vec3 clip = IsoScenePreparer::projectIsoPoint(scene.isoLayout, extent, world);
                return Vec2 { (clip.x+1)*.5f*extent.x, (1-clip.y)*.5f*extent.y };
            };
            const Vec3 inside { -2.5f, -3.5f, 3 };
            const auto hit = preparer.pickGroundPoint(scene, pixelAt(inside), extent);
            CHECK(hit.has_value());
            if (hit) {
                checkNear(hit->x, inside.x, "negative-cell paint x", .002f);
                checkNear(hit->y, inside.y, "negative-cell paint y", .002f);
                checkNear(hit->z, inside.z, "negative-cell paint height", .002f);
            }
            if (mask != 0) {
                const unsigned side = std::countr_zero(static_cast<unsigned>(mask));
                const Vec3 setback = side == 0 ? Vec3 { -2.5f, -3.975f, 3 }
                    : side == 1 ? Vec3 { -2.025f, -3.5f, 3 }
                    : side == 2 ? Vec3 { -2.5f, -3.025f, 3 }
                    : Vec3 { -2.975f, -3.5f, 3 };
                CHECK(!preparer.pickGroundPoint(scene, pixelAt(setback), extent));
                // Logical cell selection still covers the authored unit cell.
                const auto cell = preparer.pickGridCell(scene, pixelAt(setback), extent, 6, 6, 8);
                CHECK(cell == tile.cell);
            }
            for (std::size_t corner = 0; corner < unitCorners.size(); ++corner) {
                const Vec2 origin = unitCorners[corner];
                const Vec2 inward { origin.x == 0 ? 1.0f : -1.0f,
                    origin.y == 0 ? 1.0f : -1.0f };
                const Vec2 nearCorner = origin + inward*.015f;
                const Vec3 world { tile.position.x+nearCorner.x, tile.position.y+nearCorner.y, 3 };
                const bool tapered = (mask & convexCornerMasks[corner]) == convexCornerMasks[corner];
                CHECK(preparer.pickGroundPoint(scene, pixelAt(world), extent).has_value() == !tapered);
                if (tapered) {
                    CHECK(preparer.pickGridCell(scene, pixelAt(world), extent, 6, 6, 8) == tile.cell);
                    const Vec2 insideCorner = origin + inward*.105f;
                    CHECK(preparer.pickGroundPoint(scene,
                        pixelAt({ tile.position.x+insideCorner.x, tile.position.y+insideCorner.y, 3 }), extent).has_value());
                }
            }
            if (mask == 15 && variant == 1) {
                frame.tiles[0].effect = RenderSurfaceEffect::Standard;
                preparer.prepare(frame, extent, scene);
                CHECK(scene.opaqueFaceIndices.size() == patches.size());
                for (std::size_t index : scene.opaqueFaceIndices) {
                    CHECK(scene.isoFaces[index].material == PreparedSurfaceMaterial::Standard);
                    CHECK(scene.isoFaces[index].groundRimSurface);
                    CHECK(std::ranges::find(patches, scene.isoFaces[index].worldVertices) != patches.end());
                }
                CHECK(!preparer.pickGroundPoint(scene, pixelAt(inside), extent));
            }
        }
    }
    // A manifest with neither the requested module nor an island still has a
    // complete rectangular cube cap, rather than the requested tapered shape.
    const auto partial = makeManifest("CliffWallEdgeA", true);
    std::vector tiles { cliff({ 2, 2, 0 }, partial), cliff({ 3, 2, 0 }, partial),
        cliff({ 2, 3, 0 }, partial), cliff({ 1, 2, 0 }, partial) };
    processCliffWallGeometry(tiles, partial);
    CHECK(tiles[0].model.isCube());
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = frame.levelHeight = 6;
    frame.levelDepth = 2;
    frame.cameraExtent = { 0, 0, 0, 6, 6, 2 };
    frame.tiles.push_back(tiles[0]);
    PreparedRenderScene scene;
    preparer.prepare(frame, extent, scene);
    std::size_t cubeTops = 0;
    const std::array<Vec3, 4> cubeTop {{ { 2, 2, 1 }, { 3, 2, 1 }, { 3, 3, 1 }, { 2, 3, 1 } }};
    for (const auto& face : scene.isoFaces) {
        if (face.material != PreparedSurfaceMaterial::GroundSplat) continue;
        ++cubeTops;
        CHECK(!face.groundRimSurface);
        CHECK(face.worldVertices == cubeTop);
    }
    CHECK(cubeTops == 1);
}

void testLevelPreloadsEveryShapeForItsPaintedVariants()
{
    TEST("levelPreloadsEveryShapeForItsPaintedVariants");
    const auto manifest = makeManifest();
    for (uint32_t variant = 0; variant < 2; ++variant) {
        const Level level = Level::loadFromLayers({ { variant == 0 ? "u" : "x" }, { "C" } }, "cliff preload");
        const auto requirements = renderAssetRequirementsForLevel(level, manifest);
        CHECK(requirements.modelCount() == 7); // six cliff shapes plus the hero
        for (const auto& shape : cliffWallModelNames) {
            CHECK(requirements.contains(manifest.modelIdByName(shape[variant])));
            CHECK(!requirements.contains(manifest.modelIdByName(shape[1-variant])));
        }
    }
    const Level both = Level::loadFromLayers({ { "ux" }, { "C " } }, "mixed cliff preload");
    const auto requirements = renderAssetRequirementsForLevel(both, manifest);
    CHECK(requirements.modelCount() == 13);
    for (const auto& shape : cliffWallModelNames)
        for (std::string_view name : shape) CHECK(requirements.contains(manifest.modelIdByName(name)));
    const auto partial = makeManifest("CliffWallEdgeA");
    CHECK(renderAssetRequirementsForLevel(Level::loadFromLayers({ { "u" }, { "C" } }, "partial cliff preload"), partial).modelCount() == 6);
    CHECK(renderAssetRequirementsForLevel(Level::loadFromLayers({ { "." }, { "C" } }, "ordinary ground preload"), manifest).modelCount() == 1);
}

void testEditorFrameKeepsAutoSidesAndPaintableTopWithGroundProcessingDisabled()
{
    TEST("editorFrameKeepsAutoSidesAndPaintableTopWithGroundProcessingDisabled");
    const auto manifest = makeManifest();
    LevelEditor editor;
    editor.newDocument(2, 1, false);
    editor.setActiveLayer(0);
    CHECK(editor.setCell({ 0, 0, 0 }, TileType::CliffWall));
    CHECK(editor.setCell({ 1, 0, 0 }, TileType::CliffWall02));
    PresentationSettings settings;
    settings.geometry.processGroundGeometry = false;
    const auto frame = RenderFrameBuilder::buildEditor({ .manifest = manifest, .editor = editor, .settings = settings });
    const auto cliffAt = [](const RenderFrameData& source, GridPosition3 cell) {
        return std::ranges::find_if(source.tiles, [&](const auto& tile) {
            return tile.cliffWall && tile.cell == cell && !tile.isEditorPreview;
        });
    };
    const auto left = cliffAt(frame, { 0, 0, 0 });
    const auto right = cliffAt(frame, { 1, 0, 0 });
    CHECK(left != frame.tiles.end()); CHECK(right != frame.tiles.end());
    if (left == frame.tiles.end() || right == frame.tiles.end()) return;
    checkMask(*left, manifest, 13); checkMask(*right, manifest, 7);
    CHECK(left->cliffWallVariant == 0); CHECK(right->cliffWallVariant == 1);
    CHECK(left->effect == RenderSurfaceEffect::GroundSplat);
    CHECK(right->effect == RenderSurfaceEffect::GroundSplat);
    constexpr Vec2 extent { 1280, 720 };
    const IsoScenePreparer preparer;
    PreparedRenderScene scene;
    preparer.prepare(frame, extent, scene);
    for (const Vec3 expected : { Vec3 { .37f, .59f, 1 }, Vec3 { 1.37f, .59f, 1 } }) {
        const Vec3 clip = IsoScenePreparer::projectIsoPoint(scene.isoLayout, extent, expected);
        const auto hit = preparer.pickGroundPoint(scene,
            { (clip.x+1)*.5f*extent.x, (1-clip.y)*.5f*extent.y }, extent);
        CHECK(hit.has_value());
        if (hit) { checkNear(hit->x, expected.x, "editor paint x", .002f); checkNear(hit->y, expected.y, "editor paint y", .002f); checkNear(hit->z, 1, "editor flat cap", .002f); }
    }
    settings.geometry.processGroundGeometry = true;
    const auto processed = RenderFrameBuilder::buildEditor({ .manifest = manifest, .editor = editor, .settings = settings });
    const auto processedLeft = cliffAt(processed, { 0, 0, 0 });
    const auto processedRight = cliffAt(processed, { 1, 0, 0 });
    CHECK(processedLeft != processed.tiles.end()); CHECK(processedRight != processed.tiles.end());
    if (processedLeft != processed.tiles.end()) checkMask(*processedLeft, manifest, 13);
    if (processedRight != processed.tiles.end()) checkMask(*processedRight, manifest, 7);
    editor.setSelectedTile(TileType::CliffWall);
    const auto hovering = RenderFrameBuilder::buildEditor({ .manifest = manifest, .editor = editor,
        .settings = settings, .hoverCell = GridPosition3 { 2, 0, 0 } });
    const auto ghost = std::ranges::find_if(hovering.tiles, [](const auto& tile) {
        return tile.cliffWall && tile.isEditorPreview && tile.cell == GridPosition3 { 2, 0, 0 };
    });
    CHECK(ghost != hovering.tiles.end());
    if (ghost != hovering.tiles.end()) {
        CHECK(ghost->baseElevation > 0);
        checkMask(*ghost, manifest, 7);
    }
    const auto authoredRight = cliffAt(hovering, { 1, 0, 0 });
    CHECK(authoredRight != hovering.tiles.end());
    if (authoredRight != hovering.tiles.end()) checkMask(*authoredRight, manifest, 7);
}

void testWallPaintMaterialsResolveTheirPbrMapsOnBothCliffStyles()
{
    TEST("wallPaintMaterialsResolveTheirPbrMapsOnBothCliffStyles");
    auto manifest = makeManifest();
    for (std::string_view name : { "WallSlateRock", "WallMoss" }) {
        for (std::string_view suffix : { "", "Normal", "Orm" }) {
            const std::string textureName = std::string(name)+std::string(suffix);
            CHECK(!manifest.addTexture({
                .name = textureName, .path = textureName+".png", .tiling = true,
                .filter = TextureFilter::Linear,
                .colorSpace = suffix.empty() ? TextureColorSpace::Srgb : TextureColorSpace::Linear,
            }).isNone());
        }
    }
    const auto mask = manifest.addTexture({
        .name = "PaintMask", .path = "paint_mask.png",
        .filter = TextureFilter::Linear, .colorSpace = TextureColorSpace::Linear,
    });
    LevelEditor editor;
    editor.newDocument(2, 1, false);
    editor.setActiveLayer(0);
    CHECK(editor.setCell({ 0, 0, 0 }, TileType::CliffWall));
    CHECK(editor.setCell({ 1, 0, 0 }, TileType::CliffWall02));
    CHECK(editor.addGroundSplat({ "Default", "WallSlateRock", "WallMoss", "PaintMask", { 0, 1, 0 } }));
    CHECK(editor.addGroundSplat({ "Wall", "WallMoss", "WallSlateRock", "PaintMask" }));
    editor.setGroundAssignmentPainting(true);
    CHECK(editor.paintGroundSplat({ 0, 0, 0 }));
    CHECK(editor.paintGroundSplat({ 1, 0, 0 }));
    editor.setGroundAssignmentPainting(false);
    editor.showGroundAssignmentColors() = false;
    const auto frame = RenderFrameBuilder::buildEditor({ .manifest = manifest, .editor = editor, .settings = {} });
    const auto expected = groundSplatTexturesForMaterials(
        [&manifest](std::string_view name) { return manifest.findTextureIdByName(name); },
        "WallMoss", "WallSlateRock", mask);
    CHECK(expected.baseNormal == manifest.textureIdByName("WallMossNormal"));
    CHECK(expected.detailNormal == manifest.textureIdByName("WallSlateRockNormal"));
    CHECK(expected.baseOrm == manifest.textureIdByName("WallMossOrm"));
    CHECK(expected.detailOrm == manifest.textureIdByName("WallSlateRockOrm"));
    std::size_t cliffs = 0;
    for (const auto& tile : frame.tiles) {
        if (!tile.cliffWall || tile.isEditorPreview) continue;
        ++cliffs;
        CHECK(tile.effect == RenderSurfaceEffect::GroundSplat);
        CHECK(tile.groundSplat == expected);
    }
    CHECK(cliffs == 2);
    const auto requirements = renderAssetRequirementsForFrame(frame);
    for (RenderTexture texture : expected.sampledTextures()) {
        if (!texture.isNone()) CHECK(requirements.contains(texture));
    }
}

} // namespace

int main()
{
    try {
        testAllMasksAndBothVariants();
        testFilledGridStripAndCorner();
        testLayersUnsupportedNeighborsAndPreviewContext();
        testMissingModelsFailToCompleteIsland();
        testTaperedTopProfilesCoverageAndRotation();
        testOnlyConvexCornersTaperThroughNativeHeight();
        testTaperedHiddenEdgesStayCompleteAcrossJunctions();
        testTransformsFlatPaintHitsAndDeformationExclusion();
        testTaperedCapsShareDrawPaintAndShadowFootprints();
        testLevelPreloadsEveryShapeForItsPaintedVariants();
        testEditorFrameKeepsAutoSidesAndPaintableTopWithGroundProcessingDisabled();
        testWallPaintMaterialsResolveTheirPbrMapsOnBothCliffStyles();
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return 1;
    }
    std::cout << "CliffWallGeometryTests: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
