#include "TestHarness.hpp"

#include "engine/render/GroundRimSurfaceCache.hpp"
#include "engine/render/ProcessedGroundArtifact.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <vector>

namespace {

using namespace sokoban;
constexpr std::size_t headerBytes = 56;
constexpr std::size_t checksumOffset = 48;
constexpr std::size_t recordHeaderBytes = 40;
constexpr std::size_t patchBytes = 76;

RenderFrameData::Tile tile(int x, int y)
{
    return {
        .cell = { x, y, 0 }, .position = { static_cast<float>(x), static_cast<float>(y) },
        .color = { 1, 1, 1, 1 }, .height = 1, .model = { 1 },
        .effect = RenderSurfaceEffect::GroundSplat, .groundTop = true,
        .groundGeometryEligible = true, .groundRimSides = groundAllSides,
        .groundRimWidth = processedGroundArtifactRimWidth,
        .groundRimDepth = processedGroundArtifactRimDepth,
    };
}

std::vector<RenderFrameData::Tile> fixture()
{
    std::vector tiles { tile(-3, 4), tile(2, -1), tile(7, 9) };
    tiles[1].groundRimSides = groundNorthSide | groundWestSide;
    tiles[2].groundRimSides = 0;
    tiles[2].groundRimConcaveCorners = groundNorthWestCorner | groundSouthEastCorner;
    return tiles;
}

void checkSurface(const GroundRimSurface& actual, const GroundRimSurface& expected)
{
    CHECK(actual.count == expected.count);
    for (std::size_t patch = 0; patch < std::min(actual.count, expected.count); ++patch) {
        CHECK(actual.patches[patch].vertices == expected.patches[patch].vertices);
        CHECK(actual.patches[patch].normal == expected.patches[patch].normal);
        CHECK(actual.patches[patch].wallCoverage == expected.patches[patch].wallCoverage);
    }
}

void checkCache(const GroundRimSurfaceCache& cache, std::span<const RenderFrameData::Tile> tiles)
{
    for (std::size_t index = 0; index < tiles.size(); ++index) {
        const auto expected = buildGroundRimSurface(tiles[index]);
        const auto* actual = cache.surfaceForTileIndex(index);
        CHECK((actual != nullptr) == (expected.count != 0));
        if (actual) checkSurface(*actual, expected);
    }
}

void writeU32(std::vector<std::byte>& bytes, std::size_t offset, uint32_t value)
{
    for (std::size_t lane = 0; lane < 4; ++lane) {
        bytes.at(offset + lane) = static_cast<std::byte>(value >> (lane * 8));
    }
}

void writeU64(std::vector<std::byte>& bytes, std::size_t offset, uint64_t value)
{
    for (std::size_t lane = 0; lane < 8; ++lane) {
        bytes.at(offset + lane) = static_cast<std::byte>(value >> (lane * 8));
    }
}

void refreshChecksum(std::vector<std::byte>& bytes)
{
    uint64_t hash = 14695981039346656037ULL;
    for (std::size_t offset = 0; offset < bytes.size(); ++offset) {
        if (offset >= checksumOffset && offset < checksumOffset + 8) continue;
        hash ^= std::to_integer<uint8_t>(bytes[offset]);
        hash *= 1099511628211ULL;
    }
    writeU64(bytes, checksumOffset, hash);
}

void testDeterministicOwningRoundTrip()
{
    TEST("deterministicOwningRoundTrip");
    auto tiles = fixture();
    constexpr uint64_t fingerprint = 0x0102030405060708ULL;
    const auto artifact = buildProcessedGroundArtifact(tiles, fingerprint);
    CHECK(artifact.sourceFingerprint == fingerprint);
    CHECK(artifact.entries.size() == 3);
    for (std::size_t index = 1; index < artifact.entries.size(); ++index) {
        CHECK(artifact.entries[index - 1].key < artifact.entries[index].key);
    }
    const auto bytes = serializeProcessedGroundArtifact(artifact);
    CHECK(bytes[8] == std::byte { 1 });
    CHECK(bytes[16] == std::byte { 8 } && bytes[23] == std::byte { 1 });
    std::reverse(tiles.begin(), tiles.end());
    tiles.push_back(tiles[0]);
    CHECK(serializeProcessedGroundArtifact(buildProcessedGroundArtifact(tiles, fingerprint)) == bytes);
    auto parsed = parseProcessedGroundArtifact(bytes);
    CHECK(parsed.sourceFingerprint == fingerprint);
    CHECK(serializeProcessedGroundArtifact(parsed) == bytes);
    for (const auto& input : tiles) {
        const auto* found = parsed.find(groundRimSurfaceKey(input));
        CHECK(found != nullptr);
        if (found) checkSurface(*found, buildGroundRimSurface(input));
    }
    auto absentKey = groundRimSurfaceKey(tiles[0]);
    absentKey[0] += 1;
    CHECK(parsed.find(absentKey) == nullptr);
    // Unused fixed-array capacity and its bytes are not part of the artifact.
    bool checkedUnusedStorage = false;
    for (auto& entry : parsed.entries) {
        if (entry.surface.count < GroundRimSurface::capacity) {
            entry.surface.patches[entry.surface.count].normal.x = std::numeric_limits<float>::quiet_NaN();
            checkedUnusedStorage = true;
        }
    }
    CHECK(checkedUnusedStorage);
    CHECK(serializeProcessedGroundArtifact(parsed) == bytes);
    tiles.clear();
    CHECK(serializeProcessedGroundArtifact(parsed) == bytes);
    const auto empty = parseProcessedGroundArtifact(serializeProcessedGroundArtifact(
        buildProcessedGroundArtifact({}, 23)));
    CHECK(empty.sourceFingerprint == 23 && empty.entries.empty());
}

void testMalformedAndStaleArtifacts()
{
    TEST("malformedAndStaleArtifacts");
    const auto artifact = buildProcessedGroundArtifact(fixture(), 41);
    const auto valid = serializeProcessedGroundArtifact(artifact);
    const auto rejectMutation = [&](auto mutate, const char* reason) {
        auto changed = valid;
        mutate(changed);
        refreshChecksum(changed);
        checkThrows([&] { (void)parseProcessedGroundArtifact(changed); }, reason);
    };
    rejectMutation([](auto& bytes) { bytes[0] = std::byte { 0 }; }, "bad magic");
    rejectMutation([](auto& bytes) { writeU32(bytes, 8, 0); }, "old format version");
    rejectMutation([](auto& bytes) { writeU32(bytes, 12, processedGroundArtifactCompilerRevision + 1); },
        "stale compiler revision");
    rejectMutation([](auto& bytes) { writeU32(bytes, 28, 1); }, "unknown header flags");
    rejectMutation([](auto& bytes) { writeU32(bytes, 32, std::bit_cast<uint32_t>(0.13f)); },
        "stale canonical width metadata");
    rejectMutation([](auto& bytes) { writeU32(bytes, 36, std::bit_cast<uint32_t>(0.11f)); },
        "stale canonical depth metadata");
    rejectMutation([](auto& bytes) {
        writeU32(bytes, 24, static_cast<uint32_t>(processedGroundArtifactMaxEntries + 1));
    }, "bounded entry count");
    rejectMutation([](auto& bytes) { writeU64(bytes, 40, std::numeric_limits<uint64_t>::max()); },
        "overflowing payload size");
    rejectMutation([](auto& bytes) { writeU32(bytes, headerBytes + 36, 0); }, "empty surface entry");
    rejectMutation([](auto& bytes) {
        writeU32(bytes, headerBytes + 36, static_cast<uint32_t>(GroundRimSurface::capacity + 1));
    }, "bounded patch count");
    rejectMutation([](auto& bytes) { writeU32(bytes, headerBytes, 0); }, "cube geometry key");
    rejectMutation([](auto& bytes) { writeU32(bytes, headerBytes + 20, 16); }, "invalid side mask");
    rejectMutation([](auto& bytes) { writeU32(bytes, headerBytes + 24, 16); }, "invalid corner mask");
    rejectMutation([](auto& bytes) { writeU32(bytes, headerBytes + 28, 0); }, "disabled profile entry");
    rejectMutation([](auto& bytes) {
        writeU32(bytes, headerBytes + 4, std::bit_cast<uint32_t>(std::numeric_limits<float>::infinity()));
    }, "nonfinite origin");
    rejectMutation([](auto& bytes) {
        writeU32(bytes, headerBytes + recordHeaderBytes,
            std::bit_cast<uint32_t>(std::numeric_limits<float>::quiet_NaN()));
    }, "nonfinite patch vertex");
    for (float coverage : { -0.1f, 1.1f, std::numeric_limits<float>::quiet_NaN() }) {
        rejectMutation([&](auto& bytes) {
            writeU32(bytes, headerBytes + recordHeaderBytes + 48, std::bit_cast<uint32_t>(coverage));
        }, "invalid wall coverage");
    }
    rejectMutation([](auto& bytes) {
        const auto normal = headerBytes + recordHeaderBytes + 64;
        writeU32(bytes, normal, 0);
        writeU32(bytes, normal + 4, 0);
        writeU32(bytes, normal + 8, std::bit_cast<uint32_t>(-1.0f));
    }, "inverted patch normal");
    rejectMutation([](auto& bytes) {
        const auto patch = headerBytes + recordHeaderBytes;
        for (std::size_t lane = 0; lane < 12; ++lane) {
            std::swap(bytes[patch + 12 + lane], bytes[patch + 24 + lane]);
        }
    }, "inverted patch winding");
    const auto secondRecord = headerBytes + recordHeaderBytes + artifact.entries[0].surface.count * patchBytes;
    rejectMutation([&](auto& bytes) {
        std::copy_n(bytes.begin() + headerBytes, 36, bytes.begin() + static_cast<std::ptrdiff_t>(secondRecord));
    }, "duplicate or unsorted geometry keys");
    rejectMutation([](auto& bytes) {
        bytes.push_back(std::byte { 0 });
        writeU64(bytes, 40, static_cast<uint64_t>(bytes.size() - headerBytes));
    }, "trailing unreferenced payload");
    auto corrupt = valid;
    corrupt.back() ^= std::byte { 1 };
    checkThrows([&] { (void)parseProcessedGroundArtifact(corrupt); }, "payload checksum detects corruption");
    corrupt = valid;
    corrupt[16] ^= std::byte { 1 };
    checkThrows([&] { (void)parseProcessedGroundArtifact(corrupt); }, "metadata checksum protects fingerprint");
    for (const auto size : { std::size_t { 0 }, std::size_t { 7 }, headerBytes - 1, valid.size() - 1 }) {
        checkThrows([&] { (void)parseProcessedGroundArtifact(std::span(valid).first(size)); },
            "truncated artifact");
    }
    auto invalidSurface = artifact;
    invalidSurface.entries[0].surface.count = 0;
    checkThrows([&] { (void)serializeProcessedGroundArtifact(invalidSurface); }, "writer rejects invalid surfaces");
    invalidSurface = artifact;
    std::reverse(invalidSurface.entries.begin(), invalidSurface.entries.end());
    checkThrows([&] { (void)serializeProcessedGroundArtifact(invalidSurface); }, "writer requires canonical ordering");
}

void testExactImportAndRetainedProvenance()
{
    TEST("exactImportAndRetainedProvenance");
    auto tiles = fixture();
    const auto artifact = buildProcessedGroundArtifact(tiles, 51);
    GroundRimSurfaceCache cache;
    cache.update(tiles, &artifact);
    CHECK(cache.importedSurfaceCount() == 3 && cache.generatedSurfaceCount() == 0);
    CHECK(cache.reusedSurfaceCount() == 0 && cache.bakedSurfaceCount() == 3);
    CHECK(cache.bakedImportCount() == 3);
    checkCache(cache, tiles);
    const auto capacity = cache.capacityBytes();
    for (uint32_t frame = 0; frame < 64; ++frame) {
        std::rotate(tiles.begin(), tiles.begin() + 1, tiles.end());
        tiles[0].color = { 0.1f, 0.2f, 0.3f, 0.4f };
        cache.update(tiles); // Provider changes do not replace existing geometry.
        CHECK(cache.lastUpdateReused());
        CHECK(cache.importedSurfaceCount() == 0 && cache.generatedSurfaceCount() == 0);
        CHECK(cache.reusedSurfaceCount() == 3 && cache.bakedSurfaceCount() == 3);
        CHECK(cache.bakedImportCount() == 3 && cache.capacityBytes() == capacity);
    }
    checkCache(cache, tiles);
    tiles[0].groundRimWidth *= 0.8f;
    cache.update(tiles, &artifact);
    CHECK(cache.importedSurfaceCount() == 0 && cache.generatedSurfaceCount() == 1);
    CHECK(cache.reusedSurfaceCount() == 2 && cache.bakedSurfaceCount() == 2);
    CHECK(cache.bakedImportCount() == 3);
    checkCache(cache, tiles);
    const auto custom = parseProcessedGroundArtifact(serializeProcessedGroundArtifact(
        buildProcessedGroundArtifact(tiles, 52)));
    cache.update(tiles, &custom);
    CHECK(cache.lastUpdateReused() && cache.importedSurfaceCount() == 0);
    CHECK(cache.bakedSurfaceCount() == 2); // A live slot stays live on a hit.
    cache.invalidate();
    cache.update(tiles, &custom);
    CHECK(cache.importedSurfaceCount() == 3 && cache.generatedSurfaceCount() == 0);
    CHECK(cache.bakedSurfaceCount() == 3 && cache.bakedImportCount() == 6);
    checkCache(cache, tiles);
}

void testPartialMatchesAndOwningLifetime()
{
    TEST("partialMatchesAndOwningLifetime");
    auto tiles = fixture();
    GroundRimSurfaceCache cache;
    {
        const auto partial = buildProcessedGroundArtifact(std::span(tiles).first(2), 61);
        cache.update(tiles, &partial);
        CHECK(cache.importedSurfaceCount() == 2 && cache.generatedSurfaceCount() == 1);
        CHECK(cache.bakedSurfaceCount() == 2 && cache.bakedImportCount() == 2);
    }
    checkCache(cache, tiles); // Artifact storage has already been destroyed.
    const GroundRimSurfaceCache copied = cache;
    const auto* copySurface = copied.surfaceForTileIndex(0);
    CHECK(copySurface != nullptr);
    const auto original = buildGroundRimSurface(tiles[0]);
    tiles[0].position.x += 1;
    tiles[1].model = { 2 };
    cache.update(tiles);
    CHECK(cache.generatedSurfaceCount() == 2 && cache.reusedSurfaceCount() == 1);
    CHECK(cache.bakedSurfaceCount() == 0 && cache.bakedImportCount() == 2);
    if (copySurface) checkSurface(*copySurface, original);
    CHECK(copied.bakedSurfaceCount() == 2);
    checkCache(cache, tiles);
    for (auto& input : tiles) input.groundRimWidth = 0;
    cache.update(tiles);
    CHECK(cache.surfaceCount() == 0 && cache.bakedSurfaceCount() == 0);
    CHECK(cache.importedSurfaceCount() == 0 && cache.bakedImportCount() == 2);
}

void testBoundedFileLoader()
{
    TEST("boundedFileLoader");
    const auto path = std::filesystem::temp_directory_path() /
        ("sokoban_processed_ground_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".grm");
    const auto bytes = serializeProcessedGroundArtifact(buildProcessedGroundArtifact(fixture(), 71));
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        CHECK(static_cast<bool>(output));
    }
    CHECK(serializeProcessedGroundArtifact(loadProcessedGroundArtifact(path)) == bytes);
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.seekp(static_cast<std::streamoff>(processedGroundArtifactMaxBytes));
        output.put('\0');
        CHECK(static_cast<bool>(output));
    }
    checkThrows([&] { (void)loadProcessedGroundArtifact(path); }, "loader bounds file before allocating");
    std::error_code error;
    std::filesystem::remove(path, error);
    CHECK(!error);
    checkThrows([&] { (void)loadProcessedGroundArtifact(path); }, "missing optional artifact is observable to fallback caller");
}

} // namespace

int main()
{
    testDeterministicOwningRoundTrip();
    testMalformedAndStaleArtifacts();
    testExactImportAndRetainedProvenance();
    testPartialMatchesAndOwningLifetime();
    testBoundedFileLoader();
    if (failures != 0) {
        std::cerr << "ProcessedGroundArtifactTests: " << failures << " checks failed\n";
        return 1;
    }
    std::cout << "ProcessedGroundArtifactTests passed (" << checks << " checks)\n";
    return 0;
}
