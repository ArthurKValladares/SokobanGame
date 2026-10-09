#include "TestHarness.hpp"
#include "TestAssetRoot.hpp"

#include "engine/GroundTileGeometry.hpp"
#include "engine/GroundGeometry.hpp"
#include "engine/GroundLevelGeometry.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/Rules.hpp"
#include "engine/render/GroundChunkGeometry.hpp"
#include "engine/render/GroundMeshGeometry.hpp"
#include "engine/render/GroundRimSurface.hpp"
#include "engine/render/IsoScenePreparer.hpp"
#include "engine/render/RenderAssetRequirements.hpp"
#include "engine/render/RuntimeTextureCatalog.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
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

AssetManifest makeManifest(std::string_view omitted = {})
{
    std::string json = R"({"format":1,"models":[)";
    for (std::size_t shape = 0; shape < shapes.size(); ++shape) {
        for (uint32_t variant = 0; variant < groundRockVariantCount; ++variant) {
            const auto name = groundTileModelName(shape, variant);
            if (name == omitted) continue;
            const std::string suffix = (variant < 9 ? "0" : "") + std::to_string(variant + 1);
            json += "{\"name\":\"" + std::string(name) + "\",\"path\":\"custom/models/ground_modules/ground_" +
                std::string(shapes[shape]) + "_" + suffix + ".glb\",\"preserveSourceScale\":true},";
        }
    }
    json += R"({"name":"LegacyGround","path":"mock/ground.glb"},
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

void checkSelected(const RenderFrameData::Tile& tile, const AssetManifest& manifest, uint8_t expected)
{
    bool found = false;
    for (std::size_t shape = 0; shape < shapes.size(); ++shape) {
        if (tile.model != manifest.findModelIdByName(groundTileModelName(shape, tile.groundRockVariant))) continue;
        found = true;
        CHECK(rotateMask(masks[shape], tile.modelRotationQuarterTurns) == expected);
    }
    CHECK(found);
    CHECK(tile.groundModuleSideMask == expected);
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
    neighbor = ground({ 1, 0, 1 }, manifest); checkExcluded(neighbor);
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
    const auto requirements = renderAssetRequirementsForLevel(level, manifest);
    for (std::size_t shape = 0; shape < shapes.size(); ++shape) CHECK(requirements.contains(manifest.modelIdByName(groundTileModelName(shape, 0))));
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
}

void testSixtyNativeGlbsLoadWithStrictPbrBindings()
{
    TEST("sixtyNativeGlbsLoadWithStrictPbrBindings");
    const auto root = configuredTestAssetRoot();
    if (!root) return;
    const auto manifest = AssetManifest::loadFromFile(*root / "manifest.json");
    const auto catalog = collectRuntimeTextureCatalog(*root, manifest);
    for (std::size_t shape = 0; shape < shapes.size(); ++shape) {
        for (uint32_t variant = 0; variant < groundRockVariantCount; ++variant) {
            const auto model = manifest.modelIdByName(groundTileModelName(shape, variant));
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
            if (shape == 5) CHECK(mesh.indices.size() == 6); // loader-safe bottom closure; runtime draws its cap only
            for (const auto& vertex : mesh.vertices) {
                CHECK(std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y) && std::isfinite(vertex.position.z));
                CHECK(vertex.position.z >= -.00002f && vertex.position.z <= .91002f);
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
            if (shape == 5) continue;
            const auto pointOnLip = [&](Vec3 point) {
                for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3) {
                    const Vec3 a = mesh.vertices[mesh.indices[triangle]].position;
                    const Vec3 b = mesh.vertices[mesh.indices[triangle + 1]].position;
                    const Vec3 c = mesh.vertices[mesh.indices[triangle + 2]].position;
                    if (std::abs(a.z - .91f) > .00002f || std::abs(b.z - .91f) > .00002f || std::abs(c.z - .91f) > .00002f) continue;
                    const Vec2 ab { b.x - a.x, b.y - a.y }, ac { c.x - a.x, c.y - a.y }, ap { point.x - a.x, point.y - a.y };
                    const float determinant = ab.x * ac.y - ab.y * ac.x;
                    if (std::abs(determinant) < .00000001f) continue;
                    const float u = (ap.x * ac.y - ap.y * ac.x) / determinant;
                    const float v = (ab.x * ap.y - ab.y * ap.x) / determinant;
                    if (u >= -.0001f && v >= -.0001f && u + v <= 1.0001f) return true;
                }
                return false;
            };
            CHECK(std::ranges::any_of(mesh.vertices, [](const auto& vertex) { return std::abs(vertex.position.z - .91f) < .00002f; }));
            for (std::size_t side = 0; side < 4; ++side) {
                if (!(masks[shape] & (1U << side))) continue;
                for (std::size_t station = 0; station <= 20; ++station) {
                    const float u = static_cast<float>(station) / 20;
                    const Vec3 point = side == 0 ? Vec3 { u, 0, .91f } : side == 1 ? Vec3 { 1, u, .91f }
                        : side == 2 ? Vec3 { u, 1, .91f } : Vec3 { 0, u, .91f };
                    CHECK_MESSAGE(pointOnLip(point), "native seal ledge supports the whole authored lowered cap lip");
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
    std::ifstream input(*root / "custom/source/ground_modules/cap_surfaces.json");
    CHECK(input.good());
    if (!input.good()) return;
    const auto source = nlohmann::json::parse(input);
    CHECK(source.at("format") == 2);
    CHECK(source.at("models").size() == 250);
    std::size_t faces = 0;
    for (const auto& record : source.at("models")) {
        const auto shape = record.at("shapeIndex").get<std::size_t>();
        const auto variant = record.at("variant").get<uint32_t>();
        const auto concave = record.at("concaveCorners").get<uint8_t>();
        CHECK(record.at("exposedSideMask").get<uint8_t>() == masks.at(shape));
        CHECK((concave & ~allowedConcaveCorners(masks.at(shape))) == 0);
        const auto& surface = groundTileCanonicalTopSurface(shape, variant, concave);
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
        if (shape == 5 && concave == 0) CHECK(surface.count == 1);
        else CHECK(surface.count > 1);
    }
    CHECK(faces == 6430);
}

} // namespace

int main()
{
    try {
        testEveryMaskAndStyleUsesAuthoredGeometry();
        testMissingModulesAndUnsupportedNeighborsFailOpen();
        testAuthoredCapsMatchShadowsAndPaintPicking();
        testHiddenSeamsAndCanonicalRotation();
        testGameplayAndEditorVisibilityPaintAndResidency();
        testSixtyNativeGlbsLoadWithStrictPbrBindings();
        testSerializedCapsMatchTheActualBlenderSource();
    } catch (const std::exception& error) {
        std::cerr << "Unexpected exception: " << error.what() << '\n'; ++failures;
    }
    std::cout << "GroundTileGeometryTests: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
