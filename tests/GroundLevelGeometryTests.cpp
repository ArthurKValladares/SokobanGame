#include "TestHarness.hpp"
#include "ScopedTestDirectory.hpp"

#include "engine/GroundLevelGeometry.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/Rules.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace allocationTracking {

struct Header { void* base = nullptr; };
std::atomic_bool measuring = false;
std::atomic_uint64_t count = 0;

void* allocate(std::size_t size, std::size_t alignment)
{
    const std::size_t storedSize = std::max(size, std::size_t { 1 });
    if (storedSize > std::numeric_limits<std::size_t>::max() - sizeof(Header) - alignment) {
        throw std::bad_alloc();
    }
    void* base = std::malloc(storedSize + sizeof(Header) + alignment - 1);
    if (!base) throw std::bad_alloc();
    const auto first = reinterpret_cast<std::uintptr_t>(base) + sizeof(Header);
    const auto aligned = (first + alignment - 1) & ~(alignment - 1);
    (reinterpret_cast<Header*>(aligned) - 1)->base = base;
    if (measuring.load(std::memory_order_relaxed)) count.fetch_add(1, std::memory_order_relaxed);
    return reinterpret_cast<void*>(aligned);
}

void deallocate(void* pointer) noexcept
{
    if (pointer) std::free((reinterpret_cast<const Header*>(pointer) - 1)->base);
}

} // namespace allocationTracking

void* operator new(std::size_t size)
{ return allocationTracking::allocate(size, alignof(std::max_align_t)); }
void* operator new[](std::size_t size)
{ return allocationTracking::allocate(size, alignof(std::max_align_t)); }
void* operator new(std::size_t size, std::align_val_t alignment)
{ return allocationTracking::allocate(size, static_cast<std::size_t>(alignment)); }
void* operator new[](std::size_t size, std::align_val_t alignment)
{ return allocationTracking::allocate(size, static_cast<std::size_t>(alignment)); }
void operator delete(void* pointer) noexcept { allocationTracking::deallocate(pointer); }
void operator delete[](void* pointer) noexcept { allocationTracking::deallocate(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { allocationTracking::deallocate(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { allocationTracking::deallocate(pointer); }
void operator delete(void* pointer, std::align_val_t) noexcept { allocationTracking::deallocate(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { allocationTracking::deallocate(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept
{ allocationTracking::deallocate(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept
{ allocationTracking::deallocate(pointer); }

namespace {

using namespace sokoban;

AssetManifest makeManifest(std::string_view groundModel = "GroundRock01", float scale = 1,
    std::string_view canonicalPath = "custom/pbr/models/GroundRock01.glb",
    bool preserveScale = true)
{
    const std::string text = R"json({
        "format": 1,
        "models": [
            { "name": "GroundRock01", "path": ")json" + std::string(canonicalPath) +
        R"json(", "preserveSourceScale": )json" + (preserveScale ? "true" : "false") + R"json( },
            { "name": "GroundRock02", "path": "custom/pbr/models/GroundRock02.glb",
              "preserveSourceScale": true },
            { "name": "Custom", "path": "custom/other_ground.glb", "preserveSourceScale": true },
            { "name": "Hero", "path": "hero.glb", "geometry": "skinned", "role": "player" }
        ],
        "animations": [
            { "name": "Idle", "path": "idle.glb", "role": "player-idle" },
            { "name": "Move", "path": "move.glb", "role": "player-move" },
            { "name": "Push", "path": "push.glb", "role": "player-push" },
            { "name": "Death", "path": "death.glb", "role": "player-death" },
            { "name": "DeadIdle", "path": "dead_idle.glb", "role": "player-dead-idle" }
        ],
        "tiles": [
            { "tile": "Ground", "model": ")json" + std::string(groundModel) +
        R"json(", "scale": )json" + std::to_string(scale) + R"json( },
            { "tile": "Ground Rock 02", "model": "GroundRock02" }
        ]
    })json";
    return AssetManifest::parse(text);
}

Level simpleLevel()
{
    return Level::loadFromLayers({ { "..." }, { "C  " } }, "ground compiler fixture");
}

RenderFrameData gameplay(const Level& level, const AssetManifest& manifest,
    std::function<bool(GridPosition3)> visible = {})
{
    const GameState state = rules::initialState(level);
    GameplayPresentation presentation;
    presentation.resetEntities(state);
    PresentationSettings settings;
    settings.applyTileScales(manifest);
    settings.geometry.smoothGroundRim = true;
    settings.geometry.groundRimWidth = processedGroundArtifactRimWidth;
    settings.geometry.groundRimDepth = processedGroundArtifactRimDepth;
    return RenderFrameBuilder::buildGameplay({
        .manifest = manifest, .level = level, .state = state,
        .projectedState = state, .presentation = presentation, .settings = settings,
        .visibleCell = std::move(visible),
    });
}

void checkSurfaceEqual(const GroundRimSurface& actual, const GroundRimSurface& expected)
{
    CHECK(actual.count == expected.count);
    for (std::size_t index = 0; index < std::min(actual.count, expected.count); ++index) {
        CHECK(actual.patches[index].vertices == expected.patches[index].vertices);
        CHECK(actual.patches[index].normal == expected.patches[index].normal);
        CHECK(actual.patches[index].wallCoverage == expected.patches[index].wallCoverage);
    }
}

void writeBytes(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) throw std::runtime_error("cannot write ground artifact test fixture");
}

void testCompiledRuntimeParityAndLayers()
{
    TEST("compiledRuntimeParityAndLayers");
    const auto manifest = makeManifest();
    const Level level = Level::loadFromLayers({
        { ".A...", ".. ..", "....." },
        { "C    ", "   . ", "     " },
    }, "layered ground compiler fixture");
    const auto artifact = compileLevelGroundGeometry(level, manifest);
    const auto frame = gameplay(level, manifest);
    const auto runtime = buildProcessedGroundArtifact(
        { frame.tiles.data(), frame.tiles.size() }, levelGroundGeometryFingerprint(level, manifest));
    CHECK(!artifact.entries.empty());
    CHECK(serializeProcessedGroundArtifact(artifact) == serializeProcessedGroundArtifact(runtime));
    bool upperLayer = false;
    for (const auto& tile : frame.tiles) {
        if (!hasGroundRimSurface(tile)) continue;
        const auto* baked = artifact.find(groundRimSurfaceKey(tile));
        CHECK(baked != nullptr);
        if (baked) checkSurfaceEqual(*baked, buildGroundRimSurface(tile));
        upperLayer = upperLayer || tile.cell.z == 1;
    }
    CHECK(upperLayer);
    // The upper/lower grounds at the same XY are independent exact keys.
    const auto lowerOnly = gameplay(level, manifest, [](GridPosition3 cell) { return cell.z == 0; });
    for (const auto& tile : lowerOnly.tiles) if (hasGroundRimSurface(tile)) {
        CHECK(artifact.find(groundRimSurfaceKey(tile)) != nullptr);
    }
}

void testCustomAndScaledExclusions()
{
    TEST("customAndScaledExclusions");
    const auto level = simpleLevel();
    const auto canonical = makeManifest();
    const auto custom = makeManifest("Custom");
    const auto scaled = makeManifest("GroundRock01", 0.8f);
    const auto wrongPath = makeManifest("GroundRock01", 1, "custom/changed.glb");
    const auto remapped = makeManifest("GroundRock01", 1, "custom/pbr/models/GroundRock01.glb", false);
    CHECK(compileLevelGroundGeometry(level, canonical).entries.size() == 3);
    for (const AssetManifest* manifest : { &custom, &scaled, &wrongPath, &remapped }) {
        CHECK(compileLevelGroundGeometry(level, *manifest).entries.empty());
        CHECK(levelGroundGeometryFingerprint(level, *manifest) !=
            levelGroundGeometryFingerprint(level, canonical));
    }
    const auto mixed = Level::loadFromLayers({ { ".A." }, { "C  " } }, "mixed ground");
    const auto baked = compileLevelGroundGeometry(mixed, custom);
    CHECK(baked.entries.size() == 1);
    const auto runtime = gameplay(mixed, custom);
    const auto found = std::ranges::find_if(runtime.tiles, [](const auto& tile) {
        return tile.groundTop && tile.cell.x == 1;
    });
    CHECK(found != runtime.tiles.end());
    CHECK(found != runtime.tiles.end() && found->groundRimSides == groundAllSides);
    CHECK(found != runtime.tiles.end() && baked.find(groundRimSurfaceKey(*found)) != nullptr);
}

void testSemanticFingerprint()
{
    TEST("semanticFingerprint");
    const auto manifest = makeManifest();
    Level::Definition original { .layers = { { "..." }, { "C  " } } };
    const auto level = Level::loadFromDefinition(original, "semantic ground");
    auto presentationEdit = original;
    presentationEdit.cameraAngles = CameraAngles { 50, -125 };
    presentationEdit.layers[1] = { " CR" };
    presentationEdit.groundSplats = {
        { "Meadow", "Grass", "Stone", "MeadowMask", { 0, 1, 0 } },
        { "Sand", "Sand", "Mud", "SandMask", { 1, 0, 0 } },
    };
    presentationEdit.groundPaint = { { { 2, 0, 0 }, "Sand" } };
    const auto edited = Level::loadFromDefinition(presentationEdit, "presentation ground");
    const auto fingerprint = levelGroundGeometryFingerprint(level, manifest);
    CHECK(levelGroundGeometryFingerprint(edited, manifest) == fingerprint);
    auto padded = original;
    for (auto& layer : padded.layers) for (auto& row : layer) row += ' ';
    CHECK(levelGroundGeometryFingerprint(Level::loadFromDefinition(padded, "padded"), manifest) == fingerprint);
    const auto hole = Level::loadFromLayers({ { ". ." }, { "C  " } }, "ground hole");
    const auto alternate = Level::loadFromLayers({ { ".A." }, { "C  " } }, "ground model");
    const auto shifted = Level::loadFromLayers({ { " ..." }, { " C  " } }, "shifted ground");
    CHECK(levelGroundGeometryFingerprint(hole, manifest) != fingerprint);
    CHECK(levelGroundGeometryFingerprint(alternate, manifest) != fingerprint);
    CHECK(levelGroundGeometryFingerprint(shifted, manifest) != fingerprint);
    CHECK(serializeProcessedGroundArtifact(compileLevelGroundGeometry(level, manifest)) ==
        serializeProcessedGroundArtifact(compileLevelGroundGeometry(edited, manifest)));
}

void testVisibilityProfileAndWorldOriginMatching()
{
    TEST("visibilityProfileAndWorldOriginMatching");
    const auto manifest = makeManifest();
    const auto level = simpleLevel();
    const auto artifact = compileLevelGroundGeometry(level, manifest);
    const auto subset = gameplay(level, manifest, [](GridPosition3 cell) { return cell.x < 2; });
    for (const auto& tile : subset.tiles) if (hasGroundRimSurface(tile)) {
        CHECK((artifact.find(groundRimSurfaceKey(tile)) != nullptr) == (tile.cell.x == 0));
    }
    const auto full = gameplay(level, manifest);
    for (auto tile : full.tiles) if (hasGroundRimSurface(tile)) {
        CHECK(artifact.find(groundRimSurfaceKey(tile)) != nullptr);
        tile.position.x += 10;
        tile.cell.x += 10;
        CHECK(artifact.find(groundRimSurfaceKey(tile)) == nullptr);
        tile.position.x -= 10;
        tile.groundRimWidth = 0.15f;
        CHECK(artifact.find(groundRimSurfaceKey(tile)) == nullptr);
    }
}

void testArtifactPaths()
{
    TEST("artifactPaths");
    CHECK(groundGeometryArtifactPath("level3/screen3.scr") ==
        std::filesystem::path("geometry/ground/level3/screen3.grm"));
    CHECK(groundGeometryArtifactPath("overworld.scr") ==
        std::filesystem::path("geometry/ground/overworld.grm"));
    CHECK(groundGeometryArtifactPath("overworld/layout.json") ==
        groundGeometryArtifactPath("overworld.scr"));
    checkThrows([] { static_cast<void>(groundGeometryArtifactPath("../outside.scr")); }, "parent path");
    checkThrows([] { static_cast<void>(groundGeometryArtifactPath("overworld/../overworld.scr")); }, "hidden parent path");
    checkThrows([] { static_cast<void>(groundGeometryArtifactPath("layout.json")); }, "unsupported source");
    checkThrows([] { static_cast<void>(groundGeometryArtifactPath("")); }, "empty source");
    checkThrows([] { static_cast<void>(groundGeometryArtifactPath(std::filesystem::temp_directory_path() / "level.scr")); }, "absolute source");
}

void testStoreReuseLifetimeAndFallback()
{
    TEST("storeReuseLifetimeAndFallback");
    ScopedTestDirectory directory("sokoban-ground-store");
    const auto manifest = makeManifest();
    const auto level = simpleLevel();
    const std::filesystem::path source = "level3/screen3.scr";
    const auto path = directory.path() / groundGeometryArtifactPath(source);
    const auto bytes = serializeProcessedGroundArtifact(compileLevelGroundGeometry(level, manifest));
    writeBytes(path, bytes);
    RuntimeGroundGeometryStore store;
    const auto loaded = store.get(source, level, manifest, 1, directory.path());
    CHECK(loaded != nullptr);
    if (!loaded) return;
    CHECK(serializeProcessedGroundArtifact(*loaded) == bytes);
    std::filesystem::remove(path);
    allocationTracking::count.store(0);
    allocationTracking::measuring.store(true);
    bool warmEqual = true;
    for (unsigned index = 0; index < 64; ++index) {
        warmEqual = warmEqual && store.get(source, level, manifest, 1, directory.path()) == loaded;
    }
    allocationTracking::measuring.store(false);
    CHECK(warmEqual);
    CHECK(allocationTracking::count.load() == 0);
    // A visibility-triggered revision may change without authored ground edits.
    CHECK(store.get(source, level, manifest, 2, directory.path()) == loaded);
    const auto changed = Level::loadFromLayers({ { ". ." }, { "C  " } }, "changed store ground");
    CHECK(store.get(source, changed, manifest, 3, directory.path()) == nullptr);
    CHECK(serializeProcessedGroundArtifact(*loaded) == bytes);
    writeBytes(path, bytes);
    // Missing/stale outcomes are cached until invalidation or a new revision.
    CHECK(store.get(source, changed, manifest, 3, directory.path()) == nullptr);
    CHECK(store.get(source, changed, manifest, 4, directory.path()) == nullptr);
    store.invalidate();
    CHECK(store.get(source, level, manifest, 1, directory.path()) != nullptr);
    CHECK(store.get(source, level, manifest, 1, directory.path() / "other-content") == nullptr);
    CHECK(serializeProcessedGroundArtifact(*loaded) == bytes);

    auto corrupt = bytes;
    corrupt.back() ^= std::byte { 1 };
    writeBytes(path, corrupt);
    CHECK(store.get(source, level, manifest, 5, directory.path()) == nullptr);
    auto oldVersion = bytes;
    oldVersion[8] = std::byte { 0 };
    writeBytes(path, oldVersion);
    CHECK(store.get(source, level, manifest, 6, directory.path()) == nullptr);
    writeBytes(path, std::span(bytes).first(20));
    CHECK(store.get(source, level, manifest, 7, directory.path()) == nullptr);
    writeBytes(path, bytes);
    CHECK(store.get(source, level, manifest, 7, directory.path()) == nullptr);
    store.invalidate();
    const auto reloaded = store.get(source, level, manifest, 7, directory.path());
    CHECK(reloaded != nullptr && reloaded != loaded);
    CHECK(store.get("../outside.scr", level, manifest, 8, directory.path()) == nullptr);
    CHECK(serializeProcessedGroundArtifact(*loaded) == bytes);
}

} // namespace

int main()
{
    try {
        testCompiledRuntimeParityAndLayers();
        testCustomAndScaledExclusions();
        testSemanticFingerprint();
        testVisibilityProfileAndWorldOriginMatching();
        testArtifactPaths();
        testStoreReuseLifetimeAndFallback();
    } catch (const std::exception& error) {
        allocationTracking::measuring.store(false);
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return 1;
    }
    std::cout << "GroundLevelGeometryTests: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
