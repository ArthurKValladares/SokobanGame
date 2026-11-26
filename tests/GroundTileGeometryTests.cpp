#include "TestHarness.hpp"
#include "TestAssetRoot.hpp"

#include "engine/CliffWallGeometry.hpp"
#include "engine/GroundTileGeometry.hpp"
#include "engine/GroundGeometry.hpp"
#include "engine/GroundLevelGeometry.hpp"
#include "engine/OverworldMap.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/Rules.hpp"
#include "engine/TileThumbnailBake.hpp"
#include "engine/render/GroundChunkGeometry.hpp"
#include "engine/render/GroundDebris.hpp"
#include "engine/render/GroundMeshGeometry.hpp"
#include "engine/render/GroundRimSurface.hpp"
#include "engine/render/IsoScenePreparer.hpp"
#include "engine/render/MaterialRangeAllocator.hpp"
#include "engine/render/RenderAssetRequirements.hpp"
#include "engine/render/RuntimeTextureCatalog.hpp"
#include "engine/render/VulkanRenderConstants.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace sokoban;
constexpr std::array<std::string_view, 6> shapes { "island", "end", "strip", "corner", "edge", "interior" };
constexpr std::array<uint8_t, 6> masks { 15, 11, 5, 3, 1, 0 };
constexpr std::array<GridPosition, 8> neighborhoodOffsets {{
    { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 }, { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 },
}};
constexpr std::array<uint8_t, 4> cornerIncidentSides { 9, 3, 6, 12 };

uint8_t allowedConcaveCorners(uint8_t exposed)
{
    uint8_t result = 0;
    for (std::size_t corner = 0; corner < cornerIncidentSides.size(); ++corner) {
        if ((exposed & cornerIncidentSides[corner]) == 0) result |= static_cast<uint8_t>(1U << corner);
    }
    return result;
}

uint8_t expectedConcaveCorners(uint32_t pattern)
{
    uint8_t result = 0;
    for (std::size_t corner = 0; corner < cornerIncidentSides.size(); ++corner) {
        if ((pattern & cornerIncidentSides[corner]) == cornerIncidentSides[corner] &&
            (pattern & (1U << (corner + 4))) == 0) result |= static_cast<uint8_t>(1U << corner);
    }
    return result;
}

AssetManifest makeManifest(std::string_view omitted = {}, bool includeBottom = false)
{
    std::string json = R"({"format":1,"models":[)";
    for (const bool bottom : { false, true }) {
        if (bottom && !includeBottom) continue;
        for (const bool square : { false, true }) {
            for (std::size_t shape = 0; shape < shapes.size(); ++shape) {
                for (uint32_t variant = 0; variant < groundRockVariantCount; ++variant) {
                    const auto name = groundTileModelName(shape, variant, square, bottom);
                    if (name == omitted) continue;
                    const std::string suffix = (variant < 9 ? "0" : "") + std::to_string(variant + 1);
                    json += "{\"name\":\"" + std::string(name) + "\",\"path\":\"custom/models/ground_modules/ground_" +
                        (bottom ? "bottom_" : "") + (square ? "square_" : "") + std::string(shapes[shape]) + "_" + suffix + ".glb\",\"preserveSourceScale\":true},";
                }
            }
        }
    }
    // A composed overworld always stars the Lorekeeper.
    json += R"({"name":"LegacyGround","path":"mock/ground.glb"},
        {"name":"Lorekeeper","path":"mock/lorekeeper.glb","geometry":"skinned"},
        {"name":"Hero","path":"mock/hero.glb","geometry":"skinned","role":"player"}],
        "animations":[
            {"name":"Idle","path":"mock/idle.glb","role":"player-idle"},
            {"name":"Move","path":"mock/move.glb","role":"player-move"},
            {"name":"Push","path":"mock/push.glb","role":"player-push"},
            {"name":"Death","path":"mock/death.glb","role":"player-death"},
            {"name":"DeadIdle","path":"mock/dead.glb","role":"player-dead-idle"}],"tiles":[)";
    for (uint32_t variant = 0; variant < groundRockVariantCount; ++variant) {
        if (variant != 0) json += ',';
        json += "{\"tile\":\"" + std::string(tileTypeName(variant == 0 ? TileType::Ground :
            static_cast<TileType>(static_cast<uint32_t>(TileType::GroundRock02) + variant - 1))) +
            "\",\"model\":\"" + std::string(groundTileModelName(0, variant)) + "\"}";
    }
    json += "]}";
    return AssetManifest::parse(json);
}

RenderFrameData::Tile ground(GridPosition3 cell, const AssetManifest& manifest, uint32_t variant = 0)
{
    return {
        .cell = cell, .position = { static_cast<float>(cell.x), static_cast<float>(cell.y) },
        .color = { 1, 1, 1, 1 }, .baseElevation = static_cast<float>(cell.z), .height = 1,
        .model = manifest.modelIdByName(groundTileModelName(0, variant)),
        .effect = RenderSurfaceEffect::GroundSplat, .groundRockVariant = variant, .groundModule = true, .groundTop = true,
    };
}

uint8_t rotateMask(uint8_t mask, uint32_t turns)
{
    for (uint32_t turn = 0; turn < turns; ++turn) mask = static_cast<uint8_t>(((mask << 1) | (mask >> 3)) & 15);
    return mask;
}

void checkSelected(const RenderFrameData::Tile& tile, const AssetManifest& manifest, uint8_t expected,
    bool square = false, bool bottom = false)
{
    bool found = false;
    for (std::size_t shape = 0; shape < shapes.size(); ++shape) {
        if (tile.model != manifest.findModelIdByName(groundTileModelName(shape, tile.groundRockVariant, square, bottom))) continue;
        found = true;
        CHECK(rotateMask(masks[shape], tile.modelRotationQuarterTurns) == expected);
    }
    CHECK(found);
    CHECK(tile.groundModuleSideMask == expected);
    CHECK(tile.groundModuleSquare == square);
    CHECK(tile.groundModuleBottom == bottom);
    CHECK(!tile.groundGeometryEligible);
    CHECK(tile.groundRimWidth == 0 && tile.groundRimDepth == 0);
}

void testEveryMaskAndStyleUsesAuthoredGeometry()
{
    TEST("everyMaskAndStyleUsesAuthoredGeometry");
    const auto manifest = makeManifest();
    for (uint32_t variant = 0; variant < groundRockVariantCount; ++variant) {
        for (uint32_t pattern = 0; pattern < 256; ++pattern) {
            const uint8_t mask = static_cast<uint8_t>((~pattern) & groundAllSides);
            std::vector tiles { ground({ -4, -6, 2 }, manifest, variant) };
            for (std::size_t neighbor = 0; neighbor < neighborhoodOffsets.size(); ++neighbor) {
                if (!(pattern & (1U << neighbor))) continue;
                const auto offset = neighborhoodOffsets[neighbor];
                tiles.push_back(ground({ -4 + offset.x, -6 + offset.y, 2 }, manifest, (variant + 3) % 10));
            }
            processGroundTileGeometry(tiles, manifest);
            checkSelected(tiles[0], manifest, mask);
            CHECK(tiles[0].groundModuleConcaveCorners == expectedConcaveCorners(pattern));
            const auto selected = tiles[0].model;
            processGroundGeometry(tiles, manifest);
            CHECK(tiles[0].model == selected);
            CHECK(!tiles[0].groundGeometryEligible);
            CHECK(!hasGroundRimSurface(tiles[0]));
            CHECK(!isGroundChunkTileEligible(tiles[0]));
            CHECK(!isProcessableGroundRockModel(manifest.model(selected)));
            processGroundTileGeometry(tiles, manifest);
            checkSelected(tiles[0], manifest, mask); // rotations survive a second frame
            CHECK(tiles[0].groundModuleConcaveCorners == expectedConcaveCorners(pattern));
        }
    }
}

void testAuthoredQuarterTurnsKeepConnectedBodiesAndCapsAligned()
{
    TEST("authoredQuarterTurnsKeepConnectedBodiesAndCapsAligned");
    const auto manifest = makeManifest();
    for (uint8_t mask = 0; mask < 16; ++mask) {
        const auto baseModule = tileModuleForMask(mask);
        uint8_t seenTurns = 0;
        for (uint32_t turns = 0; turns < 4; ++turns) {
            std::vector tiles { ground({ 0, 0, 1 }, manifest, 2) };
            tiles.front().authoredRotationQuarterTurns = turns;
            for (std::size_t side = 0; side < 4; ++side) {
                if ((mask & (1U << side)) == 0) {
                    tiles.push_back(ground({ neighborhoodOffsets[side].x, neighborhoodOffsets[side].y, 1 }, manifest));
                }
            }
            processGroundTileGeometry(tiles, manifest);
            const auto& tile = tiles.front();
            checkSelected(tile, manifest, mask);
            CHECK(tile.model == manifest.modelIdByName(groundTileModelName(baseModule.shapeIndex, 2)));
            CHECK(tile.authoredRotationQuarterTurns == turns);
            CHECK(tile.modelRotationQuarterTurns < 4);
            seenTurns |= static_cast<uint8_t>(1U << tile.modelRotationQuarterTurns);
            if (mask == 0 || mask == groundAllSides) CHECK(tile.modelRotationQuarterTurns == turns);

            // Cap concavities stay in world space while their native vertices,
            // normals and material coverage follow the selected mesh rotation.
            const auto surface = groundTileTopSurface(tile);
            const auto& canonical = groundTileCanonicalTopSurface(baseModule.shapeIndex, 2,
                rotateMask(tile.groundModuleConcaveCorners, 4 - tile.modelRotationQuarterTurns));
            CHECK(surface.count == canonical.count);
            CHECK(surface.count == groundTileTopPatchCountFor(tile));
            for (std::size_t patch = 0; patch < surface.count; ++patch) {
                for (std::size_t vertex = 0; vertex < 4; ++vertex) {
                    Vec3 point = canonical.patches[patch].vertices[vertex];
                    for (uint32_t turn = 0; turn < tile.modelRotationQuarterTurns; ++turn) point = { 1-point.y, point.x, point.z };
                    point.z += 1;
                    CHECK(surface.patches[patch].vertices[vertex] == point);
                }
                Vec3 normal = canonical.patches[patch].normal;
                for (uint32_t turn = 0; turn < tile.modelRotationQuarterTurns; ++turn) normal = { -normal.y, normal.x, normal.z };
                CHECK(surface.patches[patch].normal == normal);
                CHECK(surface.patches[patch].wallCoverage == canonical.patches[patch].wallCoverage);
            }
            const auto once = tiles;
            processGroundTileGeometry(tiles, manifest);
            CHECK(tiles == once);
        }
        CHECK(std::popcount(seenTurns) == (mask == 0 || mask == 15 ? 4 : mask == 5 || mask == 10 ? 2 : 1));
    }
}

void testRotatedIslandsShareRenderShadowAndPaintCaps()
{
    TEST("rotatedIslandsShareRenderShadowAndPaintCaps");
    const auto manifest = makeManifest();
    constexpr Vec2 extent { 1280, 720 };
    IsoScenePreparer preparer;
    PreparedRenderScene scene;
    for (uint32_t turns = 0; turns < 4; ++turns) {
        auto tile = ground({ 2, 2, 1 }, manifest, 3);
        tile.authoredRotationQuarterTurns = turns;
        processGroundTileGeometry(std::span(&tile, 1), manifest);
        CHECK(tile.modelRotationQuarterTurns == turns);
        tile.groundSplat = GroundSplatTextures { .base = RenderTexture { 1 },
            .detail = RenderTexture { 2 }, .splatMap = RenderTexture { 3 } };
        RenderFrameData frame;
        frame.viewMode = RenderViewMode::Isometric3D;
        frame.levelWidth = frame.levelHeight = 6;
        frame.levelDepth = 3;
        frame.cameraExtent = { 0, 0, 0, 6, 6, 3 };
        frame.tiles.push_back(tile);
        preparer.prepare(frame, extent, scene);
        const auto surface = groundTileTopSurface(tile);
        CHECK(scene.shadowFaces.size() == surface.count);
        for (const auto& patch : surface.faces()) {
            CHECK(std::ranges::find(scene.shadowFaces, patch.vertices) != scene.shadowFaces.end());
            CHECK(std::ranges::any_of(scene.opaqueFaceIndices, [&](std::size_t index) {
                return scene.isoFaces[index].worldVertices == patch.vertices;
            }));
        }
        const Vec3 clip = IsoScenePreparer::projectIsoPoint(scene.isoLayout, extent, { 2.5f, 2.5f, 2 });
        const Vec2 pixel { (clip.x+1)*.5f*extent.x, (1-clip.y)*.5f*extent.y };
        const auto hit = preparer.pickGroundPoint(scene, pixel, extent);
        CHECK(hit.has_value());
        if (hit) {
            checkNear(hit->x, 2.5f, "rotated island paint x", .002f);
            checkNear(hit->y, 2.5f, "rotated island paint y", .002f);
            checkNear(hit->z, 2, "rotated island paint height", .002f);
        }
        CHECK(preparer.pickGridCell(scene, pixel, extent, 6, 6, 3) == tile.cell);
    }
}

void testMissingModulesAndUnsupportedNeighborsFailOpen()
{
    TEST("missingModulesAndUnsupportedNeighborsFailOpen");
    const auto manifest = makeManifest();
    const auto checkExcluded = [&](RenderFrameData::Tile neighbor) {
        std::array tiles { ground({ 0, 0, 0 }, manifest), neighbor };
        processGroundTileGeometry(tiles, manifest);
        checkSelected(tiles[0], manifest, 15);
    };
    auto neighbor = ground({ 1, 0, 0 }, manifest);
    neighbor.groundModule = false; neighbor.cliffWall = true; checkExcluded(neighbor);
    neighbor = ground({ 1, 0, 0 }, manifest); neighbor.color.w = .5f; checkExcluded(neighbor);
    neighbor = ground({ 1, 0, 0 }, manifest); neighbor.size.x = .8f; checkExcluded(neighbor);
    neighbor = ground({ 1, 0, 0 }, manifest); neighbor.pickOnly = true; checkExcluded(neighbor);
    neighbor = ground({ 1, 0, 0 }, manifest); neighbor.position.x += .2f; checkExcluded(neighbor);
    neighbor = ground({ 1, 0, 0 }, manifest); neighbor.baseElevation += .2f; checkExcluded(neighbor);
    neighbor = ground({ 1, 0, 0 }, manifest); neighbor.isEditorPreview = true; neighbor.color.w = .5f;
    neighbor.baseElevation = .02f;
    std::array preview { ground({ 0, 0, 0 }, manifest), neighbor };
    processGroundTileGeometry(preview, manifest);
    checkSelected(preview[0], manifest, 15);
    checkSelected(preview[1], manifest, 7);
    auto unnudged = preview[1];
    unnudged.baseElevation = 0;
    const auto previewCap = groundTileTopSurface(preview[1]);
    const auto unnudgedCap = groundTileTopSurface(unnudged);
    CHECK(previewCap.count == unnudgedCap.count);
    for (std::size_t patch = 0; patch < previewCap.count; ++patch) {
        for (std::size_t vertex = 0; vertex < 4; ++vertex) {
            checkNear(previewCap.patches[patch].vertices[vertex].z,
                unnudgedCap.patches[patch].vertices[vertex].z + .02f, "hover cap elevation");
        }
    }
    const auto partial = makeManifest("GroundRockEdge01");
    std::vector tiles { ground({ 0, 0, 0 }, partial), ground({ 1, 0, 0 }, partial),
        ground({ 0, 1, 0 }, partial), ground({ -1, 0, 0 }, partial) };
    processGroundTileGeometry(tiles, partial);
    checkSelected(tiles[0], partial, 15);
    std::array extremes { ground({ std::numeric_limits<int>::min(), 0, 0 }, manifest),
        ground({ std::numeric_limits<int>::max(), 0, 0 }, manifest) };
    processGroundTileGeometry(extremes, manifest);
    for (const auto& tile : extremes) checkSelected(tile, manifest, 15);
    auto source = manifest.model(manifest.modelIdByName("GroundRock01"));
    source.path = "custom/pbr/models/GroundRock01.glb";
    CHECK(!authoredGroundTileVariant(source));
    CHECK(isProcessableGroundRockModel(source));
    source.path = "custom/models/ground_modules/ground_end_01.glb";
    CHECK(!authoredGroundTileVariant(source));
    std::array authored { ground({ 0, 0, 0 }, manifest), ground({ 1, 0, 0 }, manifest, 9) };
    FrameArena liveArena("ground module occupancy", 4096);
    processGroundTileGeometry(authored, manifest, &liveArena);
    checkSelected(authored[0], manifest, 13); checkSelected(authored[1], manifest, 7);
    CHECK(!liveArena.exhausted());
    FrameArena tinyArena("ground module fallback", 1);
    processGroundTileGeometry(authored, manifest, &tinyArena);
    CHECK(tinyArena.exhausted());
    for (const auto& tile : authored) checkSelected(tile, manifest, 15);
}

void testRaisedContactsAndStacksUseSquareCohorts()
{
    TEST("raisedContactsAndStacksUseSquareCohorts");
    const auto manifest = makeManifest();
    // Every direct stack, step edge and step corner closes at the lower top.
    // The upper body has a full square base and retains its exposed top bevel.
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            std::array tiles { ground({ 0, 0, -2 }, manifest), ground({ x, y, -1 }, manifest, 9) };
            processGroundTileGeometry(tiles, manifest);
            checkSelected(tiles[0], manifest, 15, true);
            checkSelected(tiles[1], manifest, 15);
            CHECK(tiles[0].groundModuleConcaveCorners == 0);
            const auto surface = groundTileTopSurface(tiles[0]);
            for (const auto& patch : surface.faces()) {
                for (const auto point : patch.vertices) checkNear(point.z, -1, "square lower cap meets upper base");
            }
        }
    }
    std::vector cohort { ground({ 0, 0, 0 }, manifest), ground({ 1, 0, 0 }, manifest, 2),
        ground({ 2, 0, 0 }, manifest, 4), ground({ 2, 1, 0 }, manifest, 7),
        ground({ 0, 0, 1 }, manifest, 9), ground({ 8, 0, 0 }, manifest) };
    FrameArena arena("square cohort contacts", 4096);
    processGroundTileGeometry(cohort, manifest, &arena);
    CHECK(!arena.exhausted());
    checkSelected(cohort[0], manifest, 13, true);
    checkSelected(cohort[1], manifest, 5, true);
    checkSelected(cohort[2], manifest, 3, true);
    checkSelected(cohort[3], manifest, 14, true);
    checkSelected(cohort[4], manifest, 15);
    checkSelected(cohort[5], manifest, 15);
    const auto selected = cohort[3].model;
    processGroundTileGeometry(cohort, manifest);
    CHECK(cohort[3].model == selected); // stable across editor/runtime passes
    std::reverse(cohort.begin(), cohort.end());
    processGroundTileGeometry(cohort, manifest);
    CHECK(std::ranges::count_if(cohort, [](const auto& tile) { return tile.groundModuleSquare; }) == 4);

    // Cohort marking is recomputed after edits and uses only emitted geometry.
    std::erase_if(cohort, [](const auto& tile) { return tile.cell.z == 1; });
    processGroundTileGeometry(cohort, manifest);
    CHECK(std::ranges::none_of(cohort, [](const auto& tile) { return tile.groundModuleSquare; }));
    for (const auto neighbor : { GridPosition3 { 0, 0, 2 }, GridPosition3 { 2, 0, 1 } }) {
        std::array separated { ground({ 0, 0, 0 }, manifest), ground(neighbor, manifest) };
        processGroundTileGeometry(separated, manifest);
        checkSelected(separated[0], manifest, 15);
    }
    for (const int elevation : { std::numeric_limits<int>::min(), std::numeric_limits<int>::max() }) {
        std::array extremes { ground({ 0, 0, elevation }, manifest) };
        processGroundTileGeometry(extremes, manifest);
        checkSelected(extremes[0], manifest, 15);
    }

    // A hover tile can query a square cohort but cannot flatten real tiles.
    auto preview = ground({ 1, 0, 0 }, manifest, 3);
    preview.isEditorPreview = true;
    preview.color.w = .5f;
    preview.baseElevation = .02f;
    std::array hovering { ground({ 0, 0, 0 }, manifest), ground({ 0, 0, 1 }, manifest), preview };
    processGroundTileGeometry(hovering, manifest);
    checkSelected(hovering[0], manifest, 15, true);
    checkSelected(hovering[2], manifest, 7, true);
    auto upperPreview = ground({ 0, 0, 1 }, manifest);
    upperPreview.isEditorPreview = true;
    std::array ghostStack { ground({ 0, 0, 0 }, manifest), upperPreview };
    processGroundTileGeometry(ghostStack, manifest);
    checkSelected(ghostStack[0], manifest, 15);

    // Missing square side families keep all authored sides with a square cap.
    const auto partial = makeManifest("GroundRockSquareEdge01");
    std::array fallback { ground({ 0, 0, 0 }, partial), ground({ -1, 0, 0 }, partial),
        ground({ 1, 0, 0 }, partial), ground({ 0, 1, 0 }, partial), ground({ 0, 0, 1 }, partial) };
    processGroundTileGeometry(fallback, partial);
    checkSelected(fallback[0], partial, 15, true);
    auto mismatched = manifest.model(manifest.modelIdByName("GroundRockSquare01"));
    mismatched.path = "custom/models/ground_modules/ground_island_01.glb";
    CHECK(!authoredGroundTileVariant(mismatched));
}

void testEverySquareShapeSharesAuthoredRenderShadowAndPaintCaps()
{
    TEST("everySquareShapeSharesAuthoredRenderShadowAndPaintCaps");
    const auto manifest = makeManifest();
    IsoScenePreparer preparer;
    PreparedRenderScene scene;
    constexpr Vec2 extent { 1280, 720 };
    for (uint32_t variant = 0; variant < groundRockVariantCount; ++variant) {
        for (uint8_t mask = 0; mask < 16; ++mask) {
            std::vector tiles { ground({ 2, 2, 0 }, manifest, variant), ground({ 2, 2, 1 }, manifest) };
            for (std::size_t side = 0; side < 4; ++side) {
                if ((mask & (1U << side)) != 0) continue;
                tiles.push_back(ground({ 2 + neighborhoodOffsets[side].x, 2 + neighborhoodOffsets[side].y, 0 }, manifest));
            }
            processGroundTileGeometry(tiles, manifest);
            auto& tile = tiles[0];
            checkSelected(tile, manifest, mask, true);
            tile.groundSplat = GroundSplatTextures { .base = RenderTexture { 1 }, .detail = RenderTexture { 2 }, .splatMap = RenderTexture { 3 } };
            const auto surface = groundTileTopSurface(tile);
            CHECK(surface.count == groundTileTopPatchCountFor(tile));
            for (const auto& patch : surface.faces()) {
                CHECK(patch.normal == Vec3({ 0, 0, 1 }));
                for (std::size_t vertex = 0; vertex < patch.vertices.size(); ++vertex) {
                    checkNear(patch.vertices[vertex].z, 1, "square cap plane");
                    CHECK(patch.wallCoverage[vertex] == 0);
                }
            }
            RenderFrameData frame;
            frame.viewMode = RenderViewMode::Isometric3D;
            frame.levelWidth = frame.levelHeight = 6; frame.levelDepth = 2;
            frame.cameraExtent = { 0, 0, 0, 6, 6, 2 };
            frame.tiles.push_back(tile);
            preparer.prepare(frame, extent, scene);
            CHECK(scene.shadowFaces.size() == surface.count);
            CHECK(scene.shadowModelIndices.size() == (mask == 0 ? 0 : 1));
            CHECK(scene.opaqueModelIndices.size() == (mask == 0 ? 0 : 1));
            for (const auto& patch : surface.faces()) {
                CHECK(std::ranges::find(scene.shadowFaces, patch.vertices) != scene.shadowFaces.end());
            }
            for (const Vec3 world : { Vec3 { 2.001f, 2.001f, 1 }, Vec3 { 2.999f, 2.999f, 1 } }) {
                const Vec3 clip = IsoScenePreparer::projectIsoPoint(scene.isoLayout, extent, world);
                const Vec2 pixel { (clip.x + 1) * .5f * extent.x, (1 - clip.y) * .5f * extent.y };
                const auto hit = preparer.pickGroundPoint(scene, pixel, extent);
                CHECK(hit.has_value());
                if (hit) {
                    checkNear(hit->x, world.x, "square edge paint x", .002f);
                    checkNear(hit->y, world.y, "square edge paint y", .002f);
                    checkNear(hit->z, world.z, "square edge paint z", .002f);
                }
            }
        }
    }
}

void testAuthoredCapsMatchShadowsAndPaintPicking()
{
    TEST("authoredCapsMatchShadowsAndPaintPicking");
    const auto manifest = makeManifest();
    constexpr Vec2 extent { 1280, 720 };
    IsoScenePreparer preparer;
    PreparedRenderScene scene;
    std::size_t slopedPaintHits = 0;
    for (uint32_t variant = 0; variant < groundRockVariantCount; ++variant) {
        for (uint8_t mask = 0; mask < 16; ++mask) {
            for (uint8_t concave = 0; concave < 16; ++concave) {
            if ((concave & ~allowedConcaveCorners(mask)) != 0) continue;
            auto tile = ground({ 2, 2, 1 }, manifest, variant);
            const auto module = tileModuleForMask(mask);
            tile.model = manifest.modelIdByName(groundTileModelName(module.shapeIndex, variant));
            tile.modelRotationQuarterTurns = module.quarterTurns;
            tile.groundModuleSideMask = mask;
            tile.groundModuleConcaveCorners = concave;
            tile.groundSplat = GroundSplatTextures { .base = RenderTexture { 1 }, .detail = RenderTexture { 2 }, .splatMap = RenderTexture { 3 } };
            tile.groundSplatOrigin = { -5, -9 };
            RenderFrameData frame;
            frame.viewMode = RenderViewMode::Isometric3D;
            frame.levelWidth = frame.levelHeight = 6; frame.levelDepth = 3;
            frame.cameraExtent = { 0, 0, 0, 6, 6, 3 };
            frame.tiles.push_back(tile);
            preparer.prepare(frame, extent, scene);
            const auto surface = groundTileTopSurface(tile);
            const auto patches = surface.faces();
            CHECK(surface.count == groundTileTopPatchCountFor(tile));
            CHECK(scene.shadowFaces.size() == patches.size());
            CHECK(scene.shadowModelIndices.size() == (mask == 0 ? 0 : 1));
            CHECK(scene.opaqueModelIndices.size() == (mask == 0 ? 0 : 1));
            std::size_t capCount = 0;
            for (std::size_t index : scene.opaqueFaceIndices) {
                const auto& face = scene.isoFaces[index];
                if (face.material != PreparedSurfaceMaterial::GroundSplat) continue;
                ++capCount;
                const auto authored = std::ranges::find_if(patches, [&](const auto& patch) { return patch.vertices == face.worldVertices; });
                CHECK(authored != patches.end());
                if (authored != patches.end()) {
                    CHECK(face.normal == authored->normal);
                    CHECK(face.groundRimWallCoverage == authored->wallCoverage);
                }
                CHECK(std::ranges::find(scene.shadowFaces, face.worldVertices) != scene.shadowFaces.end());
                CHECK(face.groundSplat == tile.groundSplat);
                CHECK(face.groundSplatOrigin == tile.groundSplatOrigin);
                for (Vec3 point : face.worldVertices) CHECK(point.z >= 1.90999f && point.z <= 2.00001f);
            }
            CHECK(capCount == groundTileTopPatchCountFor(tile));
            const auto pixelAt = [&](Vec3 world) {
                const Vec3 clip = IsoScenePreparer::projectIsoPoint(scene.isoLayout, extent, world);
                return Vec2 { (clip.x + 1) * .5f * extent.x, (1 - clip.y) * .5f * extent.y };
            };
            const auto hit = preparer.pickGroundPoint(scene, pixelAt({ 2.37f, 2.59f, 2 }), extent);
            CHECK(hit.has_value());
            if (hit) { checkNear(hit->x, 2.37f, "paint x", .002f); checkNear(hit->y, 2.59f, "paint y", .002f); checkNear(hit->z, 2, "paint z", .002f); }
            for (const auto& patch : patches) {
                CHECK(patch.normal.z > 0);
                checkNear(dot(patch.normal, patch.normal), 1, "authored unit facet normal", .0001f);
                if (patch.normal.z >= .99f) continue;
                const Vec3 centroid = (patch.vertices[0] + patch.vertices[1] + patch.vertices[2]) / 3;
                const auto slopedHit = preparer.pickGroundPoint(scene, pixelAt(centroid), extent);
                CHECK(slopedHit.has_value());
                if (!slopedHit || std::abs(slopedHit->z - centroid.z) > .002f ||
                    std::abs(slopedHit->x - centroid.x) > .002f || std::abs(slopedHit->y - centroid.y) > .002f) continue;
                ++slopedPaintHits;
                CHECK(slopedHit->z < 2);
                CHECK(preparer.pickGridCell(scene, pixelAt(centroid), extent, 6, 6, 3) == tile.cell);
            }
            if (mask == 0 && concave == 0) CHECK(surface.count == 1);
            else CHECK(surface.count > 1);
            }
        }
    }
    CHECK(slopedPaintHits > 100);
}

struct GroundCapSample {
    float height;
    float wallCoverage;
};

std::optional<GroundCapSample> sampleCap(const GroundTileCapSurface& surface, Vec2 point)
{
    for (const auto& patch : surface.faces()) {
        for (const auto triangle : { std::array<std::size_t, 3> { 0, 1, 2 }, std::array<std::size_t, 3> { 0, 2, 3 } }) {
            const Vec3 a = patch.vertices[triangle[0]], b = patch.vertices[triangle[1]], c = patch.vertices[triangle[2]];
            const Vec2 ab { b.x - a.x, b.y - a.y }, ac { c.x - a.x, c.y - a.y }, ap { point.x - a.x, point.y - a.y };
            const float determinant = ab.x * ac.y - ab.y * ac.x;
            if (std::abs(determinant) < .00000001f) continue;
            const float u = (ap.x * ac.y - ap.y * ac.x) / determinant;
            const float v = (ab.x * ap.y - ab.y * ap.x) / determinant;
            if (u < -.000001f || v < -.000001f || u + v > 1.000001f) continue;
            const float wa = patch.wallCoverage[triangle[0]], wb = patch.wallCoverage[triangle[1]], wc = patch.wallCoverage[triangle[2]];
            return GroundCapSample { a.z + u * (b.z - a.z) + v * (c.z - a.z), wa + u * (wb - wa) + v * (wc - wa) };
        }
    }
    return std::nullopt;
}

void testAuthoredQuarterTurnsKeepJoinedCapTraces()
{
    TEST("authoredQuarterTurnsKeepJoinedCapTraces");
    const auto manifest = makeManifest();
    constexpr std::array<GridPosition, 10> neighbors {{
        { -1, -1 }, { 0, -1 }, { 1, -1 }, { 2, -1 }, { -1, 0 }, { 2, 0 },
        { -1, 1 }, { 0, 1 }, { 1, 1 }, { 2, 1 },
    }};
    constexpr std::array<float, 13> stations { 0, .03f, .06f, .09f, .12f, .15f, .37f, .70f, .88f, .91f, .94f, .97f, 1 };
    for (uint32_t pattern = 0; pattern < 1024; ++pattern) {
        std::vector tiles { ground({ 0, 0, 0 }, manifest, 2), ground({ 1, 0, 0 }, manifest, 7) };
        tiles[0].authoredRotationQuarterTurns = pattern & 3;
        tiles[1].authoredRotationQuarterTurns = (pattern >> 2) & 3;
        for (std::size_t neighbor = 0; neighbor < neighbors.size(); ++neighbor) {
            if (pattern & (1U << neighbor)) tiles.push_back(ground({ neighbors[neighbor].x, neighbors[neighbor].y, 0 }, manifest));
        }
        processGroundTileGeometry(tiles, manifest);
        const auto first = groundTileTopSurface(tiles[0]);
        const auto second = groundTileTopSurface(tiles[1]);
        for (float station : stations) {
            const auto a = sampleCap(first, { 1, station });
            const auto b = sampleCap(second, { 1, station });
            CHECK(a.has_value());
            CHECK(b.has_value());
            if (a && b) {
                checkNear(a->height, b->height, "rotated joined cap height", .00001f);
                checkNear(a->wallCoverage, b->wallCoverage, "rotated joined cap material coverage", .00001f);
            }
        }
    }
}

void testHiddenSeamsAndCanonicalRotation()
{
    TEST("hiddenSeamsAndCanonicalRotation");
    const auto manifest = makeManifest();
    for (uint32_t variant = 0; variant < groundRockVariantCount; ++variant) {
        for (uint8_t mask = 0; mask < 16; ++mask) {
            const auto module = tileModuleForMask(mask);
            for (uint8_t concave = 0; concave < 16; ++concave) {
                if ((concave & ~allowedConcaveCorners(mask)) != 0) continue;
                auto tile = ground({ 0, 0, 0 }, manifest, variant);
                tile.modelRotationQuarterTurns = module.quarterTurns;
                tile.groundModuleSideMask = mask;
                tile.groundModuleConcaveCorners = concave;
                const auto& canonical = groundTileCanonicalTopSurface(module.shapeIndex, variant, rotateMask(concave, 4 - module.quarterTurns));
                const auto surface = groundTileTopSurface(tile);
                CHECK(surface.count == canonical.count);
                for (std::size_t index = 0; index < surface.count; ++index) {
                    for (std::size_t vertex = 0; vertex < 4; ++vertex) {
                        Vec3 point = canonical.patches[index].vertices[vertex];
                        for (uint32_t turn = 0; turn < module.quarterTurns; ++turn) point = { 1 - point.y, point.x, point.z };
                        CHECK(surface.patches[index].vertices[vertex] == point);
                    }
                    Vec3 normal = canonical.patches[index].normal;
                    for (uint32_t turn = 0; turn < module.quarterTurns; ++turn) normal = { -normal.y, normal.x, normal.z };
                    CHECK(surface.patches[index].normal == normal);
                    CHECK(surface.patches[index].wallCoverage == canonical.patches[index].wallCoverage);
                }
            }
        }
    }

    // Every surrounding occupancy of two joined cells, with mixed native
    // styles, must produce the same height and material trace on both sides.
    constexpr std::array<GridPosition, 10> neighbors {{
        { -1, -1 }, { 0, -1 }, { 1, -1 }, { 2, -1 }, { -1, 0 }, { 2, 0 },
        { -1, 1 }, { 0, 1 }, { 1, 1 }, { 2, 1 },
    }};
    constexpr std::array<float, 13> stations { 0, .03f, .06f, .09f, .12f, .15f, .37f, .70f, .88f, .91f, .94f, .97f, 1 };
    FrameArena arena("native cap seam occupancy", 4096);
    for (uint32_t variant = 0; variant < groundRockVariantCount; ++variant) {
        for (uint32_t pattern = 0; pattern < 1024; ++pattern) {
            std::vector tiles { ground({ 0, 0, 0 }, manifest, variant), ground({ 1, 0, 0 }, manifest, (variant + pattern / 10) % 10) };
            for (std::size_t neighbor = 0; neighbor < neighbors.size(); ++neighbor) {
                if (!(pattern & (1U << neighbor))) continue;
                tiles.push_back(ground({ neighbors[neighbor].x, neighbors[neighbor].y, 0 }, manifest));
            }
            arena.reset();
            processGroundTileGeometry(tiles, manifest, &arena);
            CHECK(!arena.exhausted());
            const auto a = groundTileTopSurface(tiles[0]), b = groundTileTopSurface(tiles[1]);
            for (float station : stations) {
                const auto sa = sampleCap(a, { 1, station }), sb = sampleCap(b, { 1, station });
                CHECK(sa.has_value()); CHECK(sb.has_value());
                if (!sa || !sb) continue;
                checkNear(sa->height, sb->height, "native joined cap height", .00001f);
                checkNear(sa->wallCoverage, sb->wallCoverage, "native joined material coverage", .00001f);
            }
        }
    }
}

const RenderFrameData::Tile* groundAt(const RenderFrameData& frame, GridPosition3 cell)
{
    const auto it = std::ranges::find_if(frame.tiles, [&](const auto& tile) {
        return tile.groundModule && tile.cell == cell && !tile.isEditorPreview && !tile.pickOnly;
    });
    return it == frame.tiles.end() ? nullptr : &*it;
}

void testFloatingBottomTopologyCapsBoundsAndShadows()
{
    TEST("floatingBottomTopologyCapsBoundsAndShadows");
    const auto manifest = makeManifest({}, true);
    IsoScenePreparer preparer;
    PreparedRenderScene scene;
    constexpr Vec2 extent { 1280, 720 };
    FrameArena arena("floating bottom topology", 4096);
    for (const bool square : { false, true }) {
        for (uint32_t variant = 0; variant < groundRockVariantCount; ++variant) {
            for (uint8_t mask = 0; mask < 16; ++mask) {
                std::vector tiles { ground({ 2, 2, 0 }, manifest, variant) };
                for (std::size_t side = 0; side < 4; ++side) {
                    if ((mask & (1U << side)) == 0) {
                        tiles.push_back(ground({ 2 + neighborhoodOffsets[side].x, 2 + neighborhoodOffsets[side].y, 0 }, manifest));
                    }
                }
                if (square) tiles.push_back(ground({ 2, 2, 1 }, manifest));
                arena.reset();
                processGroundTileGeometry(tiles, manifest, &arena);
                CHECK(!arena.exhausted());
                checkSelected(tiles[0], manifest, mask, square, true);
                CHECK(authoredGroundTileVariant(manifest.model(tiles[0].model)) == variant);
                const auto surface = groundTileTopSurface(tiles[0]);
                auto regular = tiles[0];
                regular.groundModuleBottom = false;
                const auto regularSurface = groundTileTopSurface(regular);
                CHECK(surface.count == regularSurface.count);
                for (std::size_t patch = 0; patch < surface.count; ++patch) {
                    CHECK(surface.patches[patch].vertices == regularSurface.patches[patch].vertices);
                    CHECK(surface.patches[patch].normal == regularSurface.patches[patch].normal);
                    CHECK(surface.patches[patch].wallCoverage == regularSurface.patches[patch].wallCoverage);
                }
                const auto selected = tiles[0].model;
                processGroundTileGeometry(tiles, manifest);
                CHECK(tiles[0].model == selected);

                RenderFrameData frame;
                frame.viewMode = RenderViewMode::Isometric3D;
                frame.levelWidth = frame.levelHeight = 6; frame.levelDepth = 2;
                frame.cameraExtent = { 2, 2, 0, 1, 1, 1 };
                frame.tiles.push_back(tiles[0]);
                preparer.prepare(frame, extent, scene);
                CHECK(scene.opaqueModelIndices.size() == 1);
                CHECK(scene.shadowModelIndices.size() == 1); // interior undersides remain visible
                const Vec3 minimum {
                    2 - (((mask & groundWestSide) ? groundTileBottomOverhang : 0.0f) + groundDebrisHorizontalAmplitude),
                    2 - (((mask & groundNorthSide) ? groundTileBottomOverhang : 0.0f) + groundDebrisHorizontalAmplitude),
                    -(groundTileBottomDepth + groundDebrisVerticalAmplitude),
                };
                const Vec3 maximum {
                    3 + (((mask & groundEastSide) ? groundTileBottomOverhang : 0.0f) + groundDebrisHorizontalAmplitude),
                    3 + (((mask & groundSouthSide) ? groundTileBottomOverhang : 0.0f) + groundDebrisHorizontalAmplitude),
                    1,
                };
                CHECK(scene.renderables[0].worldBounds.minimum == minimum);
                CHECK(scene.renderables[0].worldBounds.maximum == maximum);
                for (const auto& patch : surface.faces()) {
                    CHECK(std::ranges::find(scene.shadowFaces, patch.vertices) != scene.shadowFaces.end());
                }
                // Picking uses the logical cell and its original authored cap.
                for (const auto& face : scene.isoFaces) {
                    for (const Vec3 point : face.worldVertices) {
                        CHECK(point.x >= 2 && point.x <= 3);
                        CHECK(point.y >= 2 && point.y <= 3);
                        CHECK(point.z >= 0);
                    }
                }
                const auto transform = IsoScenePreparer::modelTransformPoints(frame.tiles[0]);
                checkNear(transform.origin.z, 0, "bottom body original transform origin");
                checkNear(transform.zPoint.z, 1, "bottom body original transform scale");
                const Vec3 clip = IsoScenePreparer::projectIsoPoint(scene.isoLayout, extent, { 2.37f, 2.59f, 1 });
                const Vec2 pixel { (clip.x + 1) * .5f * extent.x, (1 - clip.y) * .5f * extent.y };
                const auto hit = preparer.pickGroundPoint(scene, pixel, extent);
                CHECK(hit.has_value());
                if (hit) checkNear(hit->z, 1, "floating cap paint height", .002f);
                CHECK(preparer.pickGridCell(scene, pixel, extent, 6, 6, 2) == tiles[0].cell);
                for (const float x : { minimum.x, maximum.x }) {
                    for (const float y : { minimum.y, maximum.y }) {
                        for (const float z : { minimum.z, maximum.z }) {
                            const Vec3 point { x, y, z };
                            const Vec3 boundClip = IsoScenePreparer::projectIsoPoint(scene.isoLayout, extent, point);
                            CHECK(std::abs(boundClip.x) <= 1.0001f && std::abs(boundClip.y) <= 1.0001f);
                            const Vec4 shadow = IsoScenePreparer::projectShadowPoint(scene.shadowLayout, point);
                            CHECK(std::abs(shadow.x) <= 1.0001f && std::abs(shadow.y) <= 1.0001f);
                        }
                    }
                }
            }
        }
    }

    std::array stack { ground({ 0, 0, 0 }, manifest), ground({ 0, 0, 1 }, manifest) };
    processGroundTileGeometry(stack, manifest);
    checkSelected(stack[0], manifest, 15, true, true);
    checkSelected(stack[1], manifest, 15);
    stack[1].groundModuleSupported = true;
    processGroundTileGeometry(std::span(stack).last(1), manifest);
    checkSelected(stack[1], manifest, 15); // actual lower support can be hidden
    stack[1].groundModuleSupported = false;
    processGroundTileGeometry(std::span(stack).last(1), manifest);
    checkSelected(stack[1], manifest, 15, false, true);

    auto preview = ground({ 1, 0, 0 }, manifest);
    preview.isEditorPreview = true; preview.color.w = .5f; preview.baseElevation = .02f;
    std::array hovering { ground({ 0, 0, 0 }, manifest), preview };
    processGroundTileGeometry(hovering, manifest);
    checkSelected(hovering[0], manifest, 15, false, true);
    checkSelected(hovering[1], manifest, 7, false, true);
    preview = ground({ 0, 0, 1 }, manifest);
    preview.isEditorPreview = true;
    std::array hoveringStack { ground({ 0, 0, 0 }, manifest), preview };
    processGroundTileGeometry(hoveringStack, manifest);
    checkSelected(hoveringStack[0], manifest, 15, false, true);
    checkSelected(hoveringStack[1], manifest, 15);

    const auto partial = makeManifest("GroundRockBottomEdge01", true);
    std::array fallback { ground({ 0, 0, 0 }, partial), ground({ 1, 0, 0 }, partial),
        ground({ -1, 0, 0 }, partial), ground({ 0, 1, 0 }, partial) };
    processGroundTileGeometry(fallback, partial);
    checkSelected(fallback[0], partial, 1); // missing underside keeps the complete regular family
    FrameArena tiny("floating bottom scratch exhaustion", 1);
    processGroundTileGeometry(fallback, partial, &tiny);
    CHECK(tiny.exhausted());
    for (const auto& tile : fallback) checkSelected(tile, partial, 15);
    auto mismatched = manifest.model(manifest.modelIdByName("GroundRockBottomSquare01"));
    mismatched.path = "custom/models/ground_modules/ground_bottom_island_01.glb";
    CHECK(!authoredGroundTileVariant(mismatched));

    // Surrounding floating screens cannot change an explicit screen's zoom or
    // pan. They still contribute their underside to the scene's depth range.
    RenderFrameData framed;
    framed.viewMode = RenderViewMode::Isometric3D;
    framed.levelWidth = framed.levelHeight = 6; framed.levelDepth = 2;
    framed.cameraExtent = { 2, 2, 0, 1, 1, 1 };
    auto center = ground({ 2, 2, 0 }, manifest);
    processGroundTileGeometry(std::span(&center, 1), manifest);
    framed.tiles.push_back(center);
    PreparedRenderScene isolated;
    preparer.prepare(framed, extent, isolated);
    auto distant = ground({ -100, -100, 0 }, manifest);
    processGroundTileGeometry(std::span(&distant, 1), manifest);
    framed.tiles.push_back(distant);
    preparer.prepare(framed, extent, scene);
    checkNear(scene.isoLayout.fitScale, isolated.isoLayout.fitScale, "neighboring floating screen zoom");
    CHECK(scene.isoLayout.projectedCenter == isolated.isoLayout.projectedCenter);
    CHECK(scene.isoLayout.farthestDepth > isolated.isoLayout.farthestDepth);
    framed.cameraOffset = { 1.5f, .75f };
    framed.cameraExtentTransitionTarget = RenderFrameData::CameraExtent { 4, 3, 0, 2, 2, 1 };
    framed.cameraExtentTransitionProgress = .5f;
    preparer.prepare(framed, extent, scene);
    framed.tiles.erase(framed.tiles.begin() + 1);
    preparer.prepare(framed, extent, isolated);
    checkNear(scene.isoLayout.fitScale, isolated.isoLayout.fitScale, "floating screen transition zoom");
    CHECK(scene.isoLayout.projectedCenter == isolated.isoLayout.projectedCenter);
}

void testFloatingBottomOverhangCullingAndScaledBounds()
{
    TEST("floatingBottomOverhangCullingAndScaledBounds");
    const auto manifest = makeManifest({}, true);
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = frame.levelHeight = 6;
    frame.levelDepth = 2;
    auto tile = ground({ 2, 2, 0 }, manifest);
    processGroundTileGeometry(std::span(&tile, 1), manifest);
    tile.groundModuleSideMask = groundEastSide;
    frame.tiles.push_back(tile);
    // The unit cell is outside this narrow view. Its east-side rocks enter it.
    frame.cameraOverride = RenderFrameData::CameraOverride {
        .position = { 3 + groundTileBottomOverhang * .5f, 2.5f, 1.05f },
        .forward = { 0, 0, -1 },
        .verticalFovDegrees = 1,
    };
    IsoScenePreparer preparer;
    PreparedRenderScene scene;
    constexpr Vec2 extent { 720, 720 };
    preparer.prepare(frame, extent, scene);
    CHECK(scene.renderables[0].mainSceneVisible);
    CHECK(scene.opaqueModelIndices.size() == 1);
    CHECK(intersects(frustumFromViewProjection(isoClipFromWorld(scene.isoLayout, extent)), scene.renderables[0].worldBounds));
    const auto identity = scene.renderables[0].identity;
    const auto revision = scene.renderables[0].boundsRevision;
    frame.tiles[0].groundModuleBottom = false;
    preparer.prepare(frame, extent, scene);
    CHECK(!intersects(frustumFromViewProjection(isoClipFromWorld(scene.isoLayout, extent)), scene.renderables[0].worldBounds));
    // Model draws still fail open until loaded mesh bounds are retained; the
    // visual AABB itself must nevertheless cover the newly exposed rocks.
    CHECK(scene.renderables[0].mainSceneVisible);
    CHECK(scene.opaqueModelIndices.size() == 1);
    CHECK(scene.renderables[0].identity == identity);
    CHECK(scene.renderables[0].boundsRevision > revision);
    CHECK(scene.renderables[0].worldBounds.minimum == Vec3({ 2, 2, 0 }));
    CHECK(scene.renderables[0].worldBounds.maximum == Vec3({ 3, 3, 1 }));

    // At the extreme of a bob, a fragment can enter a narrow camera frustum
    // while the full authored static underside remains outside it.
    frame.tiles[0].groundModuleBottom = true;
    frame.cameraOverride->position.x =
        3 + groundTileBottomOverhang + groundDebrisHorizontalAmplitude * .5f;
    frame.cameraOverride->position.z = -.94f;
    preparer.prepare(frame, extent, scene);
    const auto motionFrustum = frustumFromViewProjection(isoClipFromWorld(scene.isoLayout, extent));
    CHECK(intersects(motionFrustum, scene.renderables[0].worldBounds));
    CHECK(!intersects(motionFrustum, aabbFromMinMax(
        Vec3 { 2, 2, -groundTileBottomDepth },
        Vec3 { 3 + groundTileBottomOverhang, 3, 1 })));

    // Side masks are in world orientation, independent of canonical rotation.
    frame.cameraOverride.reset();
    frame.tiles[0].groundModuleBottom = true;
    frame.tiles[0].groundModuleSideMask = groundNorthSide | groundEastSide;
    frame.tiles[0].modelRotationQuarterTurns = 3;
    frame.tiles[0].size = { 2, 3 };
    frame.tiles[0].height = .5f;
    preparer.prepare(frame, extent, scene);
    CHECK(scene.renderables[0].worldBounds.minimum == Vec3({
        2 - 2 * groundDebrisHorizontalAmplitude,
        2 - 3 * (groundTileBottomOverhang + groundDebrisHorizontalAmplitude),
        -.5f * (groundTileBottomDepth + groundDebrisVerticalAmplitude) }));
    CHECK(scene.renderables[0].worldBounds.maximum == Vec3({
        4 + 2 * (groundTileBottomOverhang + groundDebrisHorizontalAmplitude),
        5 + 3 * groundDebrisHorizontalAmplitude, .5f }));
    const auto transform = IsoScenePreparer::modelTransformPoints(frame.tiles[0]);
    CHECK(std::abs(transform.xPoint.x - transform.origin.x) <= 2);
    CHECK(std::abs(transform.xPoint.y - transform.origin.y) <= 3);
    checkNear(transform.zPoint.z - transform.origin.z, .5f, "overhang does not scale the model");

    // A connected interior straddling a fractional screen boundary has no
    // exposed-side margin, so it cannot expand authored horizontal framing.
    frame.tiles[0] = tile;
    frame.tiles[0].groundModuleSideMask = 0;
    frame.cameraExtent = { 2, 2, 0, 1, 1, 1 };
    frame.cameraOffset = { groundTileBottomOverhang * .5f, .25f };
    preparer.prepare(frame, extent, scene);
    const auto interiorLayout = scene.isoLayout;
    auto outside = tile;
    outside.cell = { 1, 2, 0 };
    outside.position.x = 1;
    // Its east overhang enters the extent, but its logical cell does not.
    frame.tiles.push_back(outside);
    preparer.prepare(frame, extent, scene);
    CHECK(scene.isoLayout.fitScale == interiorLayout.fitScale);
    CHECK(scene.isoLayout.projectedCenter == interiorLayout.projectedCenter);
    frame.tiles.erase(frame.tiles.begin() + 1);
    frame.tiles[0].groundModuleSideMask = groundAllSides;
    preparer.prepare(frame, extent, scene);
    CHECK(scene.isoLayout.fitScale < interiorLayout.fitScale);
}

// How many native ground and cliff tiles of `frame` draw a model that
// `requirements` does not name. The cube needs no model.
std::size_t bodiesMissingFrom(const RenderAssetRequirements& requirements, const RenderFrameData& frame)
{
    return static_cast<std::size_t>(std::ranges::count_if(frame.tiles, [&](const auto& tile) {
        return (tile.groundModule || tile.cliffWall) && !tile.model.isCube() && !requirements.contains(tile.model);
    }));
}

void checkRequirementsCoverFrame(const RenderFrameData& frame, const RenderAssetRequirements& requirements)
{
    CHECK(std::ranges::any_of(frame.tiles, [](const auto& tile) { return tile.groundModule; }));
    CHECK(bodiesMissingFrom(requirements, frame) == 0);
}

// The frame a level opens on, with `visibleCell` limiting what is drawn.
RenderFrameData openingFrame(const Level& level, const AssetManifest& manifest,
    std::function<bool(GridPosition3)> visibleCell = {})
{
    const GameState state = rules::initialState(level);
    GameplayPresentation presentation;
    presentation.resetEntities(state);
    PresentationSettings settings;
    settings.applyTileScales(manifest);
    settings.geometry.processGroundGeometry = false;
    return RenderFrameBuilder::buildGameplay({
        .manifest = manifest, .level = level, .state = state, .projectedState = state,
        .presentation = presentation, .settings = settings, .visibleCell = std::move(visibleCell),
    });
}

void checkRequirementsCoverOpeningFrame(const Level& level, const AssetManifest& manifest,
    const RenderAssetRequirements& requirements)
{
    checkRequirementsCoverFrame(openingFrame(level, manifest), requirements);
}

void testFloatingBottomUsesActualWorldAndEditorSupport()
{
    TEST("floatingBottomUsesActualWorldAndEditorSupport");
    const auto manifest = makeManifest({}, true);
    constexpr std::array supportTypes { TileType::Ground, TileType::Wall, TileType::CliffWall,
        TileType::Decorative, TileType::Ice, TileType::Air, TileType::Water, TileType::PressurePlate };
    PresentationSettings settings;
    settings.geometry.processGroundGeometry = false;
    for (const auto support : supportTypes) {
        const bool supported = groundTileBottomSupport(support);
        const Level level = Level::loadFromLayers({ { std::string { tileTypeToChar(support) } }, { "." }, { "C" } }, "floating bottom support");
        GameState state = rules::initialState(level);
        GameplayPresentation presentation;
        presentation.resetEntities(state);
        RenderFrameBuilder::GameplayInput input {
            .manifest = manifest, .level = level, .state = state, .projectedState = state,
            .presentation = presentation, .settings = settings,
            .visibleCell = [](GridPosition3 cell) { return cell.z > 0; },
        };
        const auto gameplay = RenderFrameBuilder::buildGameplay(input);
        const auto* tile = groundAt(gameplay, { 0, 0, 1 });
        CHECK(tile);
        if (tile) {
            CHECK(tile->groundModuleSupported == supported);
            checkSelected(*tile, manifest, 15, false, !supported);
        }
        if (support == TileType::Ice) {
            const auto ice = std::ranges::find(state.movables, TileType::Ice, &GameState::Movable::type);
            CHECK(ice != state.movables.end());
            if (ice != state.movables.end()) ice->cell.x = 4;
            const auto moved = RenderFrameBuilder::buildGameplay(input);
            const auto* movedGround = groundAt(moved, { 0, 0, 1 });
            CHECK(movedGround);
            if (movedGround) checkSelected(*movedGround, manifest, 15, false, true);
        }
        // A level asks for the bodies its board draws, not for every body of
        // the variant. The rock at layer one is an island. It hangs a jagged
        // underside only over a cell nothing fixed supports; ice is the one
        // support that moves, so over ice it needs both bodies.
        const auto requirements = renderAssetRequirementsForLevel(level, manifest);
        const auto asks = [&](std::size_t shape, bool square, bool bottom) {
            return requirements.contains(manifest.modelIdByName(groundTileModelName(shape, 0, square, bottom)));
        };
        CHECK(asks(0, false, false)); // also the tile's base model, kept as the fallback
        CHECK(asks(0, false, true) == (!supported || support == TileType::Ice));
        // Ground below is drawn too: it touches the rock above, so it is
        // square, and nothing is under the lowest layer.
        CHECK(asks(0, true, true) == (support == TileType::Ground));
        CHECK(!asks(0, true, false));
        for (std::size_t shape = 1; shape < shapes.size(); ++shape) {
            for (const bool square : { false, true }) {
                for (const bool bottom : { false, true }) CHECK(!asks(shape, square, bottom));
            }
        }
        checkRequirementsCoverOpeningFrame(level, manifest, requirements);

        LevelEditor editor;
        editor.newDocument(1, 1, false);
        editor.setActiveLayer(0);
        (void)editor.setCell({ 0, 0, 0 }, support);
        editor.addLayerAbove();
        (void)editor.setCell({ 0, 0, 1 }, TileType::GroundRock10);
        editor.setActiveLayer(1); editor.setLayerLocked(true);
        const auto edited = RenderFrameBuilder::buildEditor({ .manifest = manifest, .editor = editor, .settings = settings });
        const auto* editedGround = groundAt(edited, { 0, 0, 1 });
        CHECK(editedGround);
        if (editedGround) {
            CHECK(editedGround->groundModuleSupported == supported);
            checkSelected(*editedGround, manifest, 15, false, !supported);
        }
        editor.setSelectedTile(TileType::GroundRock05);
        const auto hovered = RenderFrameBuilder::buildEditor({ .manifest = manifest, .editor = editor, .settings = settings,
            .hoverCell = GridPosition3 { 0, 0, 1 } });
        const auto ghost = std::ranges::find_if(hovered.tiles, [](const auto& item) {
            return item.groundModule && item.isEditorPreview && item.cell == GridPosition3 { 0, 0, 1 };
        });
        CHECK(ghost != hovered.tiles.end());
        if (ghost != hovered.tiles.end()) checkSelected(*ghost, manifest, 15, false, !supported);
    }
    const auto baked = tileThumbnails::buildBakeFrame(TileType::Ground, manifest, settings);
    CHECK(baked.tiles.size() == 1);
    CHECK(baked.tiles.back().groundModuleBottom);
    checkSelected(baked.tiles.back(), manifest, 15, false, true);
    const auto crop = tileThumbnails::cropFor(baked, 1024, 1024);
    IsoScenePreparer preparer;
    PreparedRenderScene scene;
    preparer.prepare(baked, { 1024, 1024 }, scene);
    constexpr float center = static_cast<float>(tileThumbnails::subjectCell);
    for (const float x : { center - groundTileBottomOverhang, center + 1 + groundTileBottomOverhang }) {
        for (const float y : { center - groundTileBottomOverhang, center + 1 + groundTileBottomOverhang }) {
            for (const float z : { -groundTileBottomDepth, 1.0f }) {
                const Vec3 clip = IsoScenePreparer::projectIsoPoint(scene.isoLayout, scene.renderExtent, { x, y, z });
                const float pixelX = (clip.x + 1) * 512, pixelY = (1 - clip.y) * 512;
                CHECK(pixelX >= static_cast<float>(crop.x) && pixelX <= static_cast<float>(crop.x) + static_cast<float>(crop.width));
                CHECK(pixelY >= static_cast<float>(crop.y) && pixelY <= static_cast<float>(crop.y) + static_cast<float>(crop.height));
            }
        }
    }
}

void testGameplayAndEditorVisibilityPaintAndResidency()
{
    TEST("gameplayAndEditorVisibilityPaintAndResidency");
    const auto manifest = makeManifest();
    const Level level = Level::loadFromLayers({ { "..." }, { "C  " } }, "native ground");
    const GameState state = rules::initialState(level);
    GameplayPresentation presentation;
    presentation.resetEntities(state);
    PresentationSettings settings;
    settings.geometry.processGroundGeometry = false;
    RenderFrameBuilder::GameplayInput input {
        .manifest = manifest, .level = level, .state = state, .projectedState = state,
        .presentation = presentation, .settings = settings, .visibleCell = [](GridPosition3 cell) { return cell.x < 2; },
    };
    const auto visible = RenderFrameBuilder::buildGameplay(input);
    const auto* edge = groundAt(visible, { 1, 0, 0 });
    CHECK(edge);
    if (edge) checkSelected(*edge, manifest, 7);
    CHECK(!groundAt(visible, { 2, 0, 0 }));
    input.visibleCell = {};
    const auto all = RenderFrameBuilder::buildGameplay(input);
    const auto* strip = groundAt(all, { 1, 0, 0 });
    CHECK(strip);
    if (strip) checkSelected(*strip, manifest, 5);
    CHECK(!levelHasLegacyGroundGeometry(level, manifest));
    CHECK(compileLevelGroundGeometry(level, manifest).entries.empty());
    RuntimeGroundGeometryStore store;
    CHECK(!store.get("level0/screen0.scr", level, manifest, 1, "native-models-need-no-grm"));
    CHECK(!store.get("level0/screen0.scr", level, manifest, 1, "native-models-need-no-grm"));
    // The row draws two ends and a strip. The island is the tile's base model
    // and stays as the fallback; no body the row does not draw is asked for.
    const auto requirements = renderAssetRequirementsForLevel(level, manifest);
    for (std::size_t shape = 0; shape < shapes.size(); ++shape) {
        CHECK(requirements.contains(manifest.modelIdByName(groundTileModelName(shape, 0))) == (shape <= 2));
        CHECK(!requirements.contains(manifest.modelIdByName(groundTileModelName(shape, 0, true))));
    }
    checkRequirementsCoverOpeningFrame(level, manifest, requirements);
    // An editor can change any cell's body with one edit, so it warms every
    // body of the variants its frame draws instead.
    CHECK(moduleBodyVariantsInFrame(all) == 1U);
    const auto everyBody = renderAssetRequirementsForModuleVariants(moduleBodyVariantsInFrame(all), manifest);
    CHECK(everyBody.modelCount() == shapes.size() * 2);
    for (std::size_t shape = 0; shape < shapes.size(); ++shape) {
        for (const bool square : { false, true }) {
            CHECK(everyBody.contains(manifest.modelIdByName(groundTileModelName(shape, 0, square))));
            CHECK(!everyBody.contains(manifest.modelIdByName(groundTileModelName(shape, 1, square))));
        }
    }
    CHECK(renderAssetRequirementsForModuleVariants(0, manifest).modelCount() == 0);
    LevelEditor editor;
    editor.newDocument(3, 1, false); editor.setActiveLayer(0);
    CHECK(editor.setCell({ 1, 0, 0 }, TileType::GroundRock10));
    CHECK(editor.setCell({ 2, 0, 0 }, TileType::Air));
    CHECK(editor.addGroundSplat({ "Default", "GroundGrass", "GroundRock", "GroundSplatMap", { 0, 1, 0 } }));
    CHECK(editor.addGroundSplat({ "Marble", "GroundGrass", "GroundRock", "GroundSplatMap" }));
    editor.setGroundAssignmentPainting(true);
    CHECK(editor.paintGroundSplat({ 1, 0, 0 }));
    editor.setGroundAssignmentPainting(false);
    editor.showGroundAssignmentColors() = true;
    const auto assignment = RenderFrameBuilder::buildEditor({ .manifest = manifest, .editor = editor, .settings = settings });
    const auto* assigned = groundAt(assignment, { 1, 0, 0 });
    CHECK(assigned);
    if (assigned) {
        checkSelected(*assigned, manifest, 7);
        CHECK(assigned->groundRockVariant == 9);
        CHECK(assigned->effect == RenderSurfaceEffect::Standard);
        CHECK(assigned->groundTop);
    }
    editor.showGroundAssignmentColors() = false;
    editor.setSelectedTile(TileType::GroundRock05);
    const auto hovering = RenderFrameBuilder::buildEditor({ .manifest = manifest, .editor = editor, .settings = settings,
        .hoverCell = GridPosition3 { 2, 0, 0 } });
    const auto ghost = std::ranges::find_if(hovering.tiles, [](const auto& tile) {
        return tile.groundModule && tile.isEditorPreview && tile.cell == GridPosition3 { 2, 0, 0 };
    });
    CHECK(ghost != hovering.tiles.end());
    if (ghost != hovering.tiles.end()) checkSelected(*ghost, manifest, 7);
    const auto* authored = groundAt(hovering, { 1, 0, 0 });
    CHECK(authored);
    if (authored) { checkSelected(*authored, manifest, 7); CHECK(authored->effect == RenderSurfaceEffect::GroundSplat); }
    CHECK(charToTileType(editor.documentLayers()[0][0][1]) == TileType::GroundRock10);

    const Level stacked = Level::loadFromLayers({ { ".." }, { ". " }, { "C " } }, "native ground stack");
    const GameState stackedState = rules::initialState(stacked);
    GameplayPresentation stackedPresentation;
    stackedPresentation.resetEntities(stackedState);
    RenderFrameBuilder::GameplayInput stackedInput {
        .manifest = manifest, .level = stacked, .state = stackedState, .projectedState = stackedState,
        .presentation = stackedPresentation, .settings = settings,
    };
    const auto stackedFrame = RenderFrameBuilder::buildGameplay(stackedInput);
    for (const GridPosition3 cell : { GridPosition3 { 0, 0, 0 }, GridPosition3 { 1, 0, 0 } }) {
        const auto* lower = groundAt(stackedFrame, cell);
        CHECK(lower);
        if (lower) CHECK(lower->groundModuleSquare && lower->groundTop);
    }
    const auto* upper = groundAt(stackedFrame, { 0, 0, 1 });
    CHECK(upper);
    if (upper) CHECK(!upper->groundModuleSquare);
    stackedInput.visibleCell = [](GridPosition3 cell) { return cell.z == 0; };
    const auto isolatedFloor = RenderFrameBuilder::buildGameplay(stackedInput);
    const auto* isolatedGround = groundAt(isolatedFloor, { 0, 0, 0 });
    CHECK(isolatedGround);
    if (isolatedGround) CHECK(!isolatedGround->groundModuleSquare);

    editor.newDocument(2, 1, false);
    editor.setActiveLayer(0);
    (void)editor.setCell({ 0, 0, 0 }, TileType::Ground);
    CHECK(editor.setCell({ 1, 0, 0 }, TileType::GroundRock10));
    editor.addLayerAbove();
    CHECK(editor.setCell({ 0, 0, 1 }, TileType::GroundRock05));
    const auto editorStack = RenderFrameBuilder::buildEditor({ .manifest = manifest, .editor = editor, .settings = settings });
    const auto* editorLower = groundAt(editorStack, { 0, 0, 0 });
    CHECK(editorLower);
    if (editorLower) CHECK(editorLower->groundModuleSquare && editorLower->groundTop);
}

// The frame an overworld opens on while exactly `screens` are drawn.
RenderFrameData overworldFrame(const OverworldMap& map, const AssetManifest& manifest,
    std::vector<OverworldScreenId> screens)
{
    return openingFrame(map.level(), manifest, [&map, screens = std::move(screens)](GridPosition3 cell) {
        const auto owner = map.screenAt(cell);
        return owner && std::ranges::find(screens, *owner) != screens.end();
    });
}

void testOverworldRequirementsFollowTheDrawnScreens()
{
    TEST("overworldRequirementsFollowTheDrawnScreens");
    const auto manifest = makeManifest();
    // Four one-row screens side by side, each of its own rock variant, so a
    // body names the screen it belongs to. The row is one strip of rock whose
    // ends are wherever the drawn screens stop.
    const auto screen = [](uint32_t variant, const char* objects) {
        const TileType rock = variant == 0 ? TileType::Ground :
            static_cast<TileType>(static_cast<uint32_t>(TileType::GroundRock02) + variant - 1);
        return Level::Definition { .layers = { { std::string(4, tileTypeToChar(rock)) }, { objects } } };
    };
    const OverworldMap map = OverworldMap::load({}, OverworldDraftOverride {
        .layout = OverworldLayout {
            .format = 3, .screenWidth = 4, .screenHeight = 1,
            .screens = {
                { .id = 1, .file = "screen1.scr", .slot = { 0, 0 } },
                { .id = 2, .file = "screen2.scr", .slot = { 1, 0 } },
                { .id = 3, .file = "screen3.scr", .slot = { 2, 0 } },
                { .id = 4, .file = "screen4.scr", .slot = { 3, 0 } },
            },
        },
        .definitions = {
            { .screen = 1, .definition = screen(0, "C   ") },
            { .screen = 2, .definition = screen(1, "    ") },
            { .screen = 3, .definition = screen(2, "    ") },
            { .screen = 4, .definition = screen(3, "    ") },
        },
    });
    constexpr std::size_t end = 1, strip = 2;
    const auto asks = [&](const RenderAssetRequirements& requirements, std::size_t shape, uint32_t variant) {
        return requirements.contains(manifest.modelIdByName(groundTileModelName(shape, variant)));
    };

    // Settled on the first screen, the second is the last one drawn, so its
    // far cell is an end. The whole level would call that cell a strip.
    const auto start = renderAssetRequirementsForOverworldNeighborhood(map, 1, manifest);
    CHECK(asks(start, end, 0) && asks(start, strip, 0));
    CHECK(asks(start, end, 1) && asks(start, strip, 1));
    CHECK(!asks(start, end, 2) && !asks(start, strip, 2));
    checkRequirementsCoverFrame(overworldFrame(map, manifest, { 1, 2 }), start);
    CHECK(!asks(renderAssetRequirementsForLevel(map.level(), manifest), end, 1));

    // One crossing ahead of the first screen is the second, which also draws
    // the third: its rock, ending there. The fourth is two crossings away.
    const auto ahead = renderAssetRequirementsForOverworldTransitions(map, 1, manifest);
    CHECK(asks(ahead, strip, 1));
    CHECK(asks(ahead, end, 2) && asks(ahead, strip, 2));
    CHECK(!asks(ahead, end, 3) && !asks(ahead, strip, 3));
    checkRequirementsCoverFrame(overworldFrame(map, manifest, { 1, 2, 3 }), ahead);

    // From the second screen either neighbour can become the active one. Each
    // crossing and each arrival draws a different run of screens, and every
    // one of them has to be covered.
    const auto middle = renderAssetRequirementsForOverworldTransitions(map, 2, manifest);
    for (const auto& drawn : std::vector<std::vector<OverworldScreenId>> {
            { 1, 2, 3 }, { 1, 2 }, { 1, 2, 3, 4 }, { 2, 3, 4 } }) {
        checkRequirementsCoverFrame(overworldFrame(map, manifest, drawn), middle);
    }
    CHECK(asks(middle, end, 1)); // arriving on the first screen stops drawing at the second
    CHECK(asks(middle, end, 2)); // crossing towards it still draws the third
    CHECK(asks(middle, end, 3) && asks(middle, strip, 3));

    CHECK(renderAssetRequirementsForOverworldTransitions(map, 99, manifest).empty());

    // The application makes the same plan a piece at a time. The board
    // content comes at once and the bodies one drawn set per step.
    OverworldTransitionAssetPlanner planner;
    CHECK(!planner.pending());
    auto stepped = planner.begin(map, 2, manifest);
    CHECK(planner.pending());
    CHECK(!asks(stepped, end, 3) && !asks(stepped, strip, 3));
    std::size_t steps = 0;
    while (planner.pending()) {
        stepped.merge(planner.step(map, manifest));
        ++steps;
    }
    CHECK(steps == 4); // two neighbours, each crossed to and arrived on
    CHECK(planner.step(map, manifest).empty());
    CHECK(stepped.modelCount() == middle.modelCount());
    auto both = stepped;
    both.merge(middle);
    CHECK(both.modelCount() == middle.modelCount());

    // Resolved drawn sets are kept. Back on the second screen nothing is
    // left to resolve. From the third, crossing back draws what crossing
    // over did and the screens around the second are known too, so only
    // arriving on the fourth is new.
    const auto again = planner.begin(map, 2, manifest);
    CHECK(!planner.pending());
    CHECK(again.modelCount() == middle.modelCount());
    (void)planner.begin(map, 3, manifest);
    steps = 0;
    while (planner.pending()) {
        (void)planner.step(map, manifest);
        ++steps;
    }
    CHECK(steps == 1);

    // cancel() drops the plan in progress, and nothing of it was resolved.
    OverworldTransitionAssetPlanner fresh;
    (void)fresh.begin(map, 1, manifest);
    CHECK(fresh.pending());
    fresh.cancel();
    CHECK(!fresh.pending());
    (void)fresh.begin(map, 1, manifest);
    CHECK(fresh.pending());

    // reset() also forgets what was resolved.
    (void)planner.begin(map, 2, manifest);
    CHECK(!planner.pending());
    planner.reset();
    CHECK(!planner.pending());
    (void)planner.begin(map, 2, manifest);
    CHECK(planner.pending());

    // Another map shares nothing with this one: a step handed the wrong map
    // abandons the plan, and a plan made for the other map starts over.
    const OverworldMap other = OverworldMap::load({}, OverworldDraftOverride {
        .layout = OverworldLayout {
            .format = 3, .screenWidth = 4, .screenHeight = 1,
            .screens = {
                { .id = 1, .file = "screen1.scr", .slot = { 0, 0 } },
                { .id = 2, .file = "screen2.scr", .slot = { 1, 0 } },
            },
        },
        .definitions = {
            { .screen = 1, .definition = screen(4, "C   ") },
            { .screen = 2, .definition = screen(5, "    ") },
        },
    });
    CHECK(other.fingerprint() != map.fingerprint());
    CHECK(fresh.step(other, manifest).empty());
    CHECK(!fresh.pending());
    auto elsewhere = fresh.begin(other, 1, manifest);
    CHECK(fresh.pending());
    while (fresh.pending()) elsewhere.merge(fresh.step(other, manifest));
    CHECK(asks(elsewhere, end, 4) && asks(elsewhere, end, 5));
    CHECK(!asks(elsewhere, end, 0) && !asks(elsewhere, strip, 1));
}

void testCampaignRequirementsCoverTheBodiesItsFramesDraw()
{
    TEST("campaignRequirementsCoverTheBodiesItsFramesDraw");
    const auto root = configuredTestAssetRoot();
    if (!root) return;
    const auto levels = root->parent_path() / "levels";
    if (!std::filesystem::is_directory(levels / "overworld")) return;
    const auto manifest = AssetManifest::loadFromFile(*root / "manifest.json");
    const auto catalog = renderAssetRequirementsForModuleVariants(~0U, manifest);
    const std::size_t everyBody = catalog.modelCount();
    CHECK(everyBody == shapes.size() * 4 * groundRockVariantCount + cliffWallModelNames.size() * 2);

    // The planner works from the level; a frame works from what it emits.
    // Every shipped board must agree, or a body would stream in late. The
    // frame's whole request is held to that: its other models, its textures
    // and its clips are planned by the same call.
    const auto covers = [](const RenderFrameData& frame, const RenderAssetRequirements& requirements) {
        RenderAssetRequirements both = requirements;
        both.merge(renderAssetRequirementsForFrame(frame));
        return bodiesMissingFrom(requirements, frame) == 0 && both.modelCount() == requirements.modelCount() &&
            both.textureCount() == requirements.textureCount() &&
            both.animationCount() == requirements.animationCount();
    };
    std::size_t puzzles = 0, uncovered = 0, mostBodies = 0;
    for (const auto& directory : std::filesystem::directory_iterator(levels)) {
        if (!directory.is_directory() || !directory.path().filename().string().starts_with("level")) continue;
        for (const auto& file : std::filesystem::directory_iterator(directory.path())) {
            if (file.path().extension() != ".scr") continue;
            const Level level = Level::loadFromFile(file.path());
            const auto requirements = renderAssetRequirementsForLevel(level, manifest);
            ++puzzles;
            if (!covers(openingFrame(level, manifest), requirements)) {
                ++uncovered;
                std::cerr << "  bodies missing for " << file.path() << '\n';
            }
            // RenderModel values are one past the manifest index.
            std::size_t bodies = 0;
            for (std::size_t index = 0; index < manifest.models().size(); ++index) {
                const RenderModel id { static_cast<uint32_t>(index + 1) };
                if (catalog.contains(id) && requirements.contains(id)) ++bodies;
            }
            mostBodies = std::max(mostBodies, bodies);
        }
    }
    CHECK(puzzles != 0);
    CHECK(uncovered == 0);
    // No board comes near the whole catalog it used to ask for.
    CHECK(mostBodies * 2 < everyBody);

    // The same for the overworld, whose frames draw a run of screens: the
    // settled view of every screen, and every step across a seam from it,
    // both while the view slides and once it has arrived.
    const OverworldMap map = OverworldMap::load(levels / "overworld");
    std::size_t crossings = 0;
    for (const auto& screen : map.screens()) {
        const auto here = map.visibleNeighborhood(screen.id);
        CHECK(covers(overworldFrame(map, manifest, here),
            renderAssetRequirementsForOverworldNeighborhood(map, screen.id, manifest)));
        const auto ahead = renderAssetRequirementsForOverworldTransitions(map, screen.id, manifest);
        for (const auto& destination : map.screens()) {
            if (std::abs(destination.slot.x - screen.slot.x) + std::abs(destination.slot.y - screen.slot.y) != 1) continue;
            ++crossings;
            const auto there = map.visibleNeighborhood(destination.id);
            auto sliding = here;
            sliding.insert(sliding.end(), there.begin(), there.end());
            CHECK(covers(overworldFrame(map, manifest, sliding), ahead));
            CHECK(covers(overworldFrame(map, manifest, there), ahead));
        }
    }
    CHECK(crossings != 0);
}

void testAllNativeGlbsLoadWithStrictPbrBindings()
{
    TEST("allNativeGlbsLoadWithStrictPbrBindings");
    const auto root = configuredTestAssetRoot();
    if (!root) return;
    const auto manifest = AssetManifest::loadFromFile(*root / "manifest.json");
    const auto catalog = collectRuntimeTextureCatalog(*root, manifest);
    for (const bool bottom : { false, true }) {
        for (const bool square : { false, true }) {
            const float lipHeight = square ? 1 : .91f;
            for (std::size_t shape = 0; shape < shapes.size(); ++shape) {
                for (uint32_t variant = 0; variant < groundRockVariantCount; ++variant) {
                    const auto model = manifest.modelIdByName(groundTileModelName(shape, variant, square, bottom));
                    const auto& source = manifest.model(model);
                    CHECK(authoredGroundTileVariant(source) == variant);
                    CHECK(!isProcessableGroundRockModel(source));
                    const auto raw = loadGltfMesh(*root / source.path, { .preserveSourceScale = true });
                    const auto& bindings = catalog.model(static_cast<uint32_t>(model.index()));
                    CHECK(bindings.materialMode == ModelMaterialMode::PrimitiveMaterials);
                    CHECK(bindings.primitiveMaterials.size() == raw.materials.size());
                    const auto mesh = loadGltfMesh(*root / source.path, {
                        .preserveSourceScale = true, .primitiveMaterials = bindings.primitiveMaterials,
                    });
                    CHECK(!mesh.vertices.empty() && !mesh.indices.empty());
                    CHECK(mesh.indices.size() % 3 == 0);
                    CHECK(mesh.indices == raw.indices);
                    CHECK(mesh.vertices.size() == raw.vertices.size());
                    if (shape == 5) CHECK(bottom ? mesh.indices.size() > 6 : mesh.indices.size() == 6);
                    if (bottom) CHECK(std::ranges::any_of(mesh.vertices, [](const auto& vertex) { return vertex.position.z < -.2f; }));
                    for (const auto& vertex : mesh.vertices) {
                        CHECK(std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y) && std::isfinite(vertex.position.z));
                        CHECK(vertex.position.z >= (bottom ? -groundTileBottomDepth : 0) - .00002f && vertex.position.z <= lipHeight + .00002f);
                        if (bottom) {
                            const float minimumX = (masks[shape] & groundWestSide) ? -groundTileBottomOverhang : 0.0f;
                            const float maximumX = 1 + ((masks[shape] & groundEastSide) ? groundTileBottomOverhang : 0.0f);
                            const float minimumY = (masks[shape] & groundNorthSide) ? -groundTileBottomOverhang : 0.0f;
                            const float maximumY = 1 + ((masks[shape] & groundSouthSide) ? groundTileBottomOverhang : 0.0f);
                            CHECK(vertex.position.x >= minimumX - .00002f && vertex.position.x <= maximumX + .00002f);
                            CHECK(vertex.position.y >= minimumY - .00002f && vertex.position.y <= maximumY + .00002f);
                        }
                        CHECK(std::isfinite(vertex.normal.x) && std::isfinite(vertex.normal.y) && std::isfinite(vertex.normal.z));
                        CHECK(std::abs(dot(vertex.normal, vertex.normal) - 1) < .0001f);
                        CHECK(std::isfinite(vertex.tangent.x) && std::isfinite(vertex.tangent.y) && std::isfinite(vertex.tangent.z) && std::isfinite(vertex.tangent.w));
                        CHECK(vertex.materialIndex < mesh.materials.size());
                    }
                    for (std::size_t slot = 0; slot < mesh.materials.size(); ++slot) {
                        CHECK(mesh.materials[slot].alphaMode == MaterialAlphaMode::Opaque);
                        if (!bindings.primitiveMaterials[slot].bindBaseColorTexture) {
                            CHECK(mesh.materials[slot].baseColorFactor == raw.materials[slot].baseColorFactor);
                        }
                        if (bindings.primitiveMaterials[slot].normalTextureIndex) CHECK(mesh.materials[slot].normalTexture != 0);
                        if (bindings.primitiveMaterials[slot].metallicRoughnessTextureIndex) CHECK(mesh.materials[slot].metallicRoughnessTexture != 0);
                    }
                    if (bottom && variant == 0 && (shape == 3 || shape == 4)) {
                        IsoScenePreparer preparer;
                        PreparedRenderScene scene;
                        for (uint32_t turn = 0; turn < 4; ++turn) {
                            RenderFrameData frame;
                            frame.viewMode = RenderViewMode::Isometric3D;
                            frame.levelWidth = frame.levelHeight = 6;
                            frame.levelDepth = 2;
                            auto tile = ground({ 2, 2, 0 }, manifest);
                            tile.model = model;
                            tile.groundModuleBottom = true;
                            tile.groundModuleSquare = square;
                            tile.groundModuleSideMask = rotateMask(masks[shape], turn);
                            tile.modelRotationQuarterTurns = turn;
                            frame.tiles.push_back(tile);
                            preparer.prepare(frame, { 1280, 720 }, scene);
                            const auto& bounds = scene.renderables[0].worldBounds;
                            const auto transform = IsoScenePreparer::modelTransformPoints(tile);
                            for (const auto& vertex : mesh.vertices) {
                                const Vec3 world = transform.origin +
                                    (transform.xPoint - transform.origin) * vertex.position.x +
                                    (transform.yPoint - transform.origin) * vertex.position.y +
                                    (transform.zPoint - transform.origin) * vertex.position.z;
                                CHECK(world.x >= bounds.minimum.x - .00002f && world.x <= bounds.maximum.x + .00002f);
                                CHECK(world.y >= bounds.minimum.y - .00002f && world.y <= bounds.maximum.y + .00002f);
                                CHECK(world.z >= bounds.minimum.z - .00002f && world.z <= bounds.maximum.z + .00002f);
                            }
                        }
                    }
                    if (shape == 5) continue;
                    const auto pointOnLip = [&](Vec3 point) {
                        for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3) {
                            const Vec3 a = mesh.vertices[mesh.indices[triangle]].position;
                            const Vec3 b = mesh.vertices[mesh.indices[triangle + 1]].position;
                            const Vec3 c = mesh.vertices[mesh.indices[triangle + 2]].position;
                            if (std::abs(a.z - lipHeight) > .00002f || std::abs(b.z - lipHeight) > .00002f || std::abs(c.z - lipHeight) > .00002f) continue;
                            const Vec2 ab { b.x - a.x, b.y - a.y }, ac { c.x - a.x, c.y - a.y }, ap { point.x - a.x, point.y - a.y };
                            const float determinant = ab.x * ac.y - ab.y * ac.x;
                            if (std::abs(determinant) < .00000001f) continue;
                            const float u = (ap.x * ac.y - ap.y * ac.x) / determinant;
                            const float v = (ab.x * ap.y - ab.y * ap.x) / determinant;
                            if (u >= -.0001f && v >= -.0001f && u + v <= 1.0001f) return true;
                        }
                        return false;
                    };
                    CHECK(std::ranges::any_of(mesh.vertices, [lipHeight](const auto& vertex) { return std::abs(vertex.position.z - lipHeight) < .00002f; }));
                    for (std::size_t side = 0; side < 4; ++side) {
                        if (!(masks[shape] & (1U << side))) continue;
                        for (std::size_t station = 0; station <= 20; ++station) {
                            const float u = static_cast<float>(station) / 20;
                            const Vec3 point = side == 0 ? Vec3 { u, 0, lipHeight } : side == 1 ? Vec3 { 1, u, lipHeight }
                                : side == 2 ? Vec3 { u, 1, lipHeight } : Vec3 { 0, u, lipHeight };
                            CHECK_MESSAGE(pointOnLip(point), "native seal ledge supports the whole authored cap lip");
                        }
                    }
                }
            }
        }
    }
    for (uint32_t variant = 0; variant < groundRockVariantCount; ++variant) {
        const TileType tile = variant == 0 ? TileType::Ground :
            static_cast<TileType>(static_cast<uint32_t>(TileType::GroundRock02) + variant - 1);
        CHECK(manifest.modelForTile(tile) == manifest.modelIdByName(groundTileModelName(0, variant)));
        const auto visual = tileVisual(tile, { 0, 0, 0 }, manifest, {});
        CHECK(visual.groundModule && visual.groundRockVariant == variant);
    }
}

void testSerializedCapsMatchTheActualBlenderSource()
{
    TEST("serializedCapsMatchTheActualBlenderSource");
    const auto root = configuredTestAssetRoot();
    if (!root) return;
    for (const bool square : { false, true }) {
        std::ifstream input(*root / (square ? "custom/source/ground_modules/square_cap_surfaces.json" :
            "custom/source/ground_modules/cap_surfaces.json"));
        CHECK(input.good());
        if (!input.good()) return;
        const auto source = nlohmann::json::parse(input);
        CHECK(source.at("format") == 2);
        CHECK(source.at("models").size() == (square ? 60 : 250));
        std::size_t faces = 0;
        for (const auto& record : source.at("models")) {
            const auto shape = record.at("shapeIndex").get<std::size_t>();
            const auto variant = record.at("variant").get<uint32_t>();
            const auto concave = record.at("concaveCorners").get<uint8_t>();
            CHECK(record.at("exposedSideMask").get<uint8_t>() == masks.at(shape));
            CHECK((concave & ~allowedConcaveCorners(masks.at(shape))) == 0);
            const auto& surface = groundTileCanonicalTopSurface(shape, variant, concave, square);
            CHECK(surface.count == record.at("faces").size());
            CHECK(surface.count <= groundTileTopPatchCapacity);
            faces += surface.count;
            for (std::size_t face = 0; face < surface.count; ++face) {
                const auto& indices = record.at("faces").at(face);
                const auto& patch = surface.patches[face];
                for (std::size_t vertex = 0; vertex < 4; ++vertex) {
                    const auto sourceVertex = std::min(vertex, indices.size() - 1);
                    const auto& coordinates = record.at("vertices").at(indices.at(sourceVertex).get<std::size_t>());
                    CHECK(patch.vertices[vertex] == Vec3({ coordinates.at(0).get<float>(), coordinates.at(1).get<float>(), coordinates.at(2).get<float>() }));
                    CHECK(patch.wallCoverage[vertex] == record.at("wallCoverage").at(face).at(sourceVertex).get<float>());
                }
                const auto& normal = record.at("normals").at(face);
                CHECK(patch.normal == Vec3({ normal.at(0).get<float>(), normal.at(1).get<float>(), normal.at(2).get<float>() }));
                CHECK(patch.normal.z > 0);
                checkNear(dot(patch.normal, patch.normal), 1, "source facet normal", .0002f);
            }
            if (square || (shape == 5 && concave == 0)) CHECK(surface.count == 1);
            else CHECK(surface.count > 1);
        }
        CHECK(faces == (square ? 60 : 6430));
    }
}

void testFullCatalogMaterialsFitTheGpuTable()
{
    TEST("fullCatalogMaterialsFitTheGpuTable");
    const auto root = configuredTestAssetRoot();
    if (!root) return;
    const auto manifest = AssetManifest::loadFromFile(*root / "manifest.json");
    MaterialRangeAllocator allocator;
    allocator.reset(1); // Published-nothing fallback material.
    uint64_t required = 1;
    const auto materialCount = [&](const std::string& path) {
        const auto dependencies = inspectGltfAssetDependencies(*root / path);
        // The mesh loader supplies a default slot for documents without one.
        return static_cast<uint32_t>(std::max<std::size_t>(dependencies.materials.size(), 1));
    };
    for (const auto& model : manifest.models()) {
        uint32_t count = materialCount(model.path);
        if (model.geometry == ModelGeometry::Skinned) {
            for (const auto& attachment : model.attachments) count += materialCount(attachment.path);
        }
        required += count;
        CHECK_MESSAGE(allocator.allocate(count, maxModelMaterials).has_value(), model.name.c_str());
    }
    CHECK(required <= maxModelMaterials);
    CHECK(allocator.highWaterMark() == required);
    std::cout << "Full authored catalog materials: " << required << " / " << maxModelMaterials << '\n';
}

} // namespace

int main()
{
    try {
        testEveryMaskAndStyleUsesAuthoredGeometry();
        testAuthoredQuarterTurnsKeepConnectedBodiesAndCapsAligned();
        testRotatedIslandsShareRenderShadowAndPaintCaps();
        testAuthoredQuarterTurnsKeepJoinedCapTraces();
        testMissingModulesAndUnsupportedNeighborsFailOpen();
        testRaisedContactsAndStacksUseSquareCohorts();
        testEverySquareShapeSharesAuthoredRenderShadowAndPaintCaps();
        testAuthoredCapsMatchShadowsAndPaintPicking();
        testHiddenSeamsAndCanonicalRotation();
        testFloatingBottomTopologyCapsBoundsAndShadows();
        testFloatingBottomOverhangCullingAndScaledBounds();
        testFloatingBottomUsesActualWorldAndEditorSupport();
        testGameplayAndEditorVisibilityPaintAndResidency();
        testOverworldRequirementsFollowTheDrawnScreens();
        testCampaignRequirementsCoverTheBodiesItsFramesDraw();
        testAllNativeGlbsLoadWithStrictPbrBindings();
        testSerializedCapsMatchTheActualBlenderSource();
        testFullCatalogMaterialsFitTheGpuTable();
    } catch (const std::exception& error) {
        std::cerr << "Unexpected exception: " << error.what() << '\n'; ++failures;
    }
    std::cout << "GroundTileGeometryTests: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
