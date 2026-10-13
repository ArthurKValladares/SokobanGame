#include "TestHarness.hpp"

#include "engine/ArenaArray.hpp"
#include "engine/FrameArena.hpp"
#include "engine/FrameArray.hpp"
#include "engine/Log.hpp"
#include "engine/render/RenderTypes.hpp"

#include <algorithm>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

[[nodiscard]] bool isAlignedTo(const void* pointer, std::size_t alignment)
{
    return reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0;
}

void testArenaMemoryTracksOwnersRatherThanFrameUsage()
{
    TEST("arenaMemoryTracksOwnersRatherThanFrameUsage");
    using namespace sokoban;
    const auto baseline = frameArenaMemoryStats();
    std::size_t observedPeak = baseline.peakReservedBytes;
    FrameArray<int> escaped;
    {
        FrameArena first("ledger first", 257);
        auto stats = frameArenaMemoryStats();
        CHECK(stats.arenaCount == baseline.arenaCount + 1);
        CHECK(stats.reservedBytes == baseline.reservedBytes + 257);
        CHECK(stats.peakReservedBytes == std::max(
            baseline.peakReservedBytes, baseline.reservedBytes + 257));
        {
            FrameArena second("ledger second", 1024);
            stats = frameArenaMemoryStats();
            CHECK(stats.arenaCount == baseline.arenaCount + 2);
            CHECK(stats.reservedBytes == baseline.reservedBytes + 1281);
            CHECK(stats.peakReservedBytes == std::max(
                baseline.peakReservedBytes, baseline.reservedBytes + 1281));
            observedPeak = stats.peakReservedBytes;
            CHECK(first.allocate(64, 1) != nullptr);
            FrameArray<int> values(first, 4);
            CHECK(values.push_back(7));
            auto movedView = std::move(values);
            escaped = movedView.clone();
            CHECK(escaped.front() == 7);
            first.reset();
            CHECK(first.bytesUsed() == 0);
            CHECK(first.highWaterBytes() >= 64 + 4 * sizeof(int));
            CHECK(second.allocate(2048, 1) == nullptr);
            CHECK(second.exhausted());
            CHECK(second.bytesUsed() == 0);
            second.reset();
            CHECK(!second.exhausted());
            stats = frameArenaMemoryStats();
            CHECK(stats.arenaCount == baseline.arenaCount + 2);
            CHECK(stats.reservedBytes == baseline.reservedBytes + 1281);
            CHECK(stats.peakReservedBytes == observedPeak);
        }
        stats = frameArenaMemoryStats();
        CHECK(stats.arenaCount == baseline.arenaCount + 1);
        CHECK(stats.reservedBytes == baseline.reservedBytes + 257);
        CHECK(stats.peakReservedBytes == observedPeak);
    }
    const auto released = frameArenaMemoryStats();
    CHECK(released.arenaCount == baseline.arenaCount);
    CHECK(released.reservedBytes == baseline.reservedBytes);
    CHECK(released.peakReservedBytes == observedPeak);
    CHECK(escaped.front() == 7); // The owning clone is not another arena.

    checkThrows([] { FrameArena impossible("failed ledger constructor", SIZE_MAX); },
        "failed arena construction must not acquire a reservation");
    const auto failed = frameArenaMemoryStats();
    CHECK(failed.arenaCount == released.arenaCount);
    CHECK(failed.reservedBytes == released.reservedBytes);
    CHECK(failed.peakReservedBytes == released.peakReservedBytes);
}

void testConcurrentArenaOwnersPublishCoherentMemorySnapshots()
{
    TEST("concurrentArenaOwnersPublishCoherentMemorySnapshots");
    using namespace sokoban;
    const auto baseline = frameArenaMemoryStats();
    constexpr std::size_t ownerBytes = 512;
    constexpr int rounds = 16;
    std::barrier phases(3);
    const auto owner = [&] {
        for (int round = 0; round < rounds; ++round) {
            phases.arrive_and_wait();
            {
                FrameArena arena("concurrent ledger owner", ownerBytes);
                phases.arrive_and_wait();
                phases.arrive_and_wait();
            }
            phases.arrive_and_wait();
        }
    };
    std::thread first(owner);
    std::thread second(owner);
    const auto checkCoherent = [&] {
        const auto stats = frameArenaMemoryStats();
        CHECK(stats.arenaCount >= baseline.arenaCount);
        CHECK(stats.arenaCount <= baseline.arenaCount + 2);
        CHECK(stats.reservedBytes >= baseline.reservedBytes);
        CHECK(stats.reservedBytes - baseline.reservedBytes ==
            (stats.arenaCount - baseline.arenaCount) * ownerBytes);
        CHECK(stats.peakReservedBytes >= stats.reservedBytes);
    };
    for (int round = 0; round < rounds; ++round) {
        phases.arrive_and_wait();
        checkCoherent(); // Observe construction without relying on scheduling or delays.
        phases.arrive_and_wait();
        const auto overlapping = frameArenaMemoryStats();
        CHECK(overlapping.arenaCount == baseline.arenaCount + 2);
        CHECK(overlapping.reservedBytes == baseline.reservedBytes + 2 * ownerBytes);
        CHECK(overlapping.peakReservedBytes == std::max(
            baseline.peakReservedBytes, baseline.reservedBytes + 2 * ownerBytes));
        phases.arrive_and_wait();
        checkCoherent(); // Destruction can be in progress, but the pair must agree.
        phases.arrive_and_wait();
        const auto released = frameArenaMemoryStats();
        CHECK(released.arenaCount == baseline.arenaCount);
        CHECK(released.reservedBytes == baseline.reservedBytes);
    }
    first.join();
    second.join();
}

void testAlignmentExhaustionWarningReportsZeroUsableBytes()
{
    TEST("alignmentExhaustionWarningReportsZeroUsableBytes");
    using namespace sokoban;
    log::reset();
    log::configure({ .stderrEnabled = false });
    log::setHistoryEnabled(true);
    FrameArena arena("padding warning fixture", 1);
    CHECK(arena.allocate(1, 1) != nullptr);
    CHECK(arena.allocate(1, 8) == nullptr);
    CHECK(arena.bytesUsed() == 1);
    CHECK(arena.bytesRemaining() == 0);
    CHECK(arena.exhausted());
    const auto history = log::historySnapshot();
    const auto warning = std::ranges::find_if(history.entries, [](const auto& entry) {
        return entry.message.find("Frame arena 'padding warning fixture'") != std::string::npos;
    });
    CHECK(warning != history.entries.end());
    if (warning != history.entries.end()) {
        CHECK(warning->level == log::Level::Warning);
        CHECK(warning->category == log::Category::Rendering);
        CHECK(warning->message.find("0 usable of 1") != std::string::npos);
    }
    log::reset();
}

void testBumpsAndReports()
{
    sokoban::FrameArena arena("test", 1024);
    CHECK(arena.capacityBytes() == 1024);
    CHECK(arena.bytesUsed() == 0);
    CHECK(arena.bytesRemaining() == 1024);

    void* const first = arena.allocate(64, 8);
    CHECK(first != nullptr);
    CHECK(arena.bytesUsed() == 64);

    // Consecutive requests are adjacent: the head only moves forward.
    void* const second = arena.allocate(32, 8);
    CHECK(second != nullptr);
    CHECK(static_cast<std::byte*>(second) ==
        static_cast<std::byte*>(first) + 64);
    CHECK(arena.bytesUsed() == 96);
    CHECK(arena.bytesRemaining() == 1024 - 96);
    CHECK(!arena.exhausted());
}

void testAlignmentPadding()
{
    sokoban::FrameArena arena("test", 1024);

    // One byte, so every later request has to skip padding to reach its
    // alignment. The padding is spent, not reclaimed - that is the trade the
    // allocator makes for having no bookkeeping.
    CHECK(arena.allocate(1, 1) != nullptr);
    CHECK(arena.bytesUsed() == 1);

    void* const aligned16 = arena.allocate(16, 16);
    CHECK(aligned16 != nullptr);
    CHECK(isAlignedTo(aligned16, 16));
    CHECK(arena.bytesUsed() == 32);

    void* const aligned64 = arena.allocate(8, 64);
    CHECK(aligned64 != nullptr);
    CHECK(isAlignedTo(aligned64, 64));
}

void testResetReturnsTheWholeArena()
{
    sokoban::FrameArena arena("test", 256);

    // The failure this pins is a reset that only appears to work: the second
    // frame must get exactly as much room as the first, not what the first
    // happened to leave behind.
    const void* firstFrameStart = nullptr;
    for (int frame = 0; frame < 100; ++frame) {
        arena.reset();
        CHECK(arena.bytesUsed() == 0);
        CHECK(arena.bytesRemaining() == 256);

        void* const block = arena.allocate(256, 1);
        CHECK(block != nullptr);
        if (frame == 0) {
            firstFrameStart = block;
        }
        // Every frame hands out the same address, because the head goes back
        // to the start rather than the arena finding more memory somewhere.
        CHECK(block == firstFrameStart);
        CHECK(arena.bytesRemaining() == 0);
    }
    CHECK(arena.highWaterBytes() == 256);
}

void testExhaustionIsReportedNotFatal()
{
    sokoban::FrameArena arena("test", 128);
    CHECK(arena.allocate(100, 1) != nullptr);

    // Does not fit, so it is refused rather than served from somewhere else.
    CHECK(arena.allocate(64, 1) == nullptr);
    CHECK(arena.exhausted());
    // A refused request must not move the head; the arena stays usable.
    CHECK(arena.bytesUsed() == 100);
    CHECK(arena.allocate(28, 1) != nullptr);
    CHECK(arena.bytesRemaining() == 0);

    arena.reset();
    CHECK(!arena.exhausted());
    CHECK(arena.allocate(128, 1) != nullptr);
}

void testAllocateUninitializedRespectsType()
{
    sokoban::FrameArena arena("test", 1024);
    struct alignas(32) Wide {
        double values[4];
    };

    Wide* const wide = arena.allocateUninitialized<Wide>(4);
    CHECK(wide != nullptr);
    CHECK(isAlignedTo(wide, alignof(Wide)));
    CHECK(arena.bytesUsed() >= 4 * sizeof(Wide));

    // A count that would overflow the byte computation is refused rather than
    // wrapping around into a small, valid-looking request.
    CHECK(arena.allocateUninitialized<Wide>(SIZE_MAX / 2) == nullptr);
}

void testArenaArrayFillsAndDropsRatherThanGrowing()
{
    sokoban::FrameArena arena(
        "test", sokoban::arenaBytesFor<int>(8));
    sokoban::ArenaArray<int> values(arena, 8);
    CHECK(values.capacity() == 8);
    CHECK(values.empty());

    // The array takes its whole capacity in one bump, so filling it costs no
    // further arena space at all.
    const std::size_t usedAfterConstruction = arena.bytesUsed();
    for (int i = 0; i < 8; ++i) {
        CHECK(values.push_back(i));
    }
    CHECK(arena.bytesUsed() == usedAfterConstruction);
    CHECK(values.size() == 8);
    CHECK(values.full());
    CHECK(values.front() == 0);
    CHECK(values.back() == 7);
    CHECK(values[3] == 3);

    // Past capacity the value is dropped and counted. Growing instead would
    // strand the buffer it outgrew, because the arena cannot take one back.
    CHECK(!values.push_back(8));
    CHECK(!values.push_back(9));
    CHECK(values.size() == 8);
    CHECK(values.droppedCount() == 2);
    CHECK(!arena.exhausted());

    int sum = 0;
    for (const int value : values) {
        sum += value;
    }
    CHECK(sum == 28);
}

void testArenaArrayFromExhaustedArenaIsEmptyNotNull()
{
    sokoban::FrameArena arena("test", 8);
    sokoban::ArenaArray<int> values(arena, 64);
    CHECK(arena.exhausted());
    CHECK(values.capacity() == 0);
    CHECK(values.empty());
    // Nothing is written through the null storage; the pushes are dropped.
    CHECK(!values.push_back(1));
    CHECK(values.droppedCount() == 1);
    CHECK(values.begin() == values.end());
}

void testFrameArrayPreservesOwningCopySemantics()
{
    sokoban::FrameArena arena("test", sokoban::arenaBytesFor<int>(4));
    sokoban::FrameArray<int> live(arena, 4);
    CHECK(live.arenaBacked());
    CHECK(live.push_back(10));
    CHECK(live.push_back(20));

    // Copying an arena-backed frame is an explicit lifetime escape, so the
    // copy becomes ordinary owning storage and survives the arena reset.
    sokoban::FrameArray<int> owned = live;
    CHECK(!owned.arenaBacked());
    arena.reset();
    CHECK(owned.size() == 2);
    CHECK(owned[0] == 10);
    CHECK(owned[1] == 20);

    owned = { 1, 2, 3 };
    CHECK(owned.size() == 3);
    CHECK(owned.back() == 3);
    owned.erase(owned.begin() + 1);
    CHECK(owned.size() == 2);
    CHECK(owned.back() == 3);
}

void testArenaBackedFrameArrayKeepsItsFixedCapacity()
{
    sokoban::FrameArena arena("test", sokoban::arenaBytesFor<int>(2));
    sokoban::FrameArray<int> values(arena, 2);
    values.reserve(1000);
    CHECK(values.capacity() == 2);
    CHECK(values.push_back(1));
    CHECK(values.push_back(2));
    CHECK(!values.push_back(3));
    CHECK(values.droppedCount() == 1);
}

void testEmptyFrameArraysRemainWritableThroughCopiesAndMoves()
{
    TEST("emptyFrameArraysRemainWritableThroughCopiesAndMoves");
    sokoban::FrameArray<int> empty;
    CHECK(empty.empty());
    CHECK(empty.capacity() == 0);
    CHECK(empty.data() == nullptr);
    CHECK(!empty.arenaBacked());
    empty.reserve(0);
    empty.clear();
    CHECK(empty.erase(empty.begin(), empty.end()) == empty.end());

    auto copied = empty.clone();
    auto moved = std::move(copied);
    CHECK(moved.empty());
    CHECK(!moved.arenaBacked());
    CHECK(moved.push_back(7));
    CHECK(moved.front() == 7);
    CHECK(empty.empty());
    copied.reserve(2);
    CHECK(copied.push_back(9));
    CHECK(copied.back() == 9);

    sokoban::FrameArena arena("empty copy", sokoban::arenaBytesFor<int>(2));
    sokoban::FrameArray<int> arenaEmpty(arena, 2);
    auto escaped = arenaEmpty.clone();
    CHECK(!escaped.arenaBacked());
    CHECK(escaped.capacity() == 0);
    arena.reset();
    CHECK(escaped.push_back(11));
    CHECK(escaped.front() == 11);
    moved = empty;
    CHECK(moved.empty());
    CHECK(moved.capacity() == 0);
    CHECK(moved.push_back(13));
    CHECK(moved.front() == 13);

    auto owningDestination = std::move(moved);
    CHECK(owningDestination.front() == 13);
    CHECK(moved.push_back(17));
    CHECK(moved.back() == 17);
    CHECK(owningDestination.front() == 13);
}

struct ThrowingCopy {
    int value = 0;
    inline static bool fail = false;

    explicit ThrowingCopy(int initial) : value(initial) {}
    ThrowingCopy(const ThrowingCopy& other) : value(other.value)
    {
        if (fail) {
            throw std::runtime_error("injected element copy failure");
        }
    }
    ThrowingCopy& operator=(const ThrowingCopy&) = default;
};

void testFailedFirstFrameArrayWriteLeavesUsableStorage()
{
    TEST("failedFirstFrameArrayWriteLeavesUsableStorage");
    sokoban::FrameArray<ThrowingCopy> values;
    const ThrowingCopy value(23);
    ThrowingCopy::fail = true;
    checkThrows([&] { values.push_back(value); }, "first element copy throws");
    ThrowingCopy::fail = false;
    CHECK(values.empty());
    CHECK(!values.arenaBacked());
    values.clear();
    CHECK(values.push_back(value));
    CHECK(values.front().value == 23);

    sokoban::FrameArray<int> reserved;
    checkThrows([&] { reserved.reserve(SIZE_MAX); }, "impossible first reserve throws");
    CHECK(reserved.empty());
    CHECK(reserved.capacity() == 0);
    CHECK(!reserved.arenaBacked());
    CHECK(reserved.push_back(29));
    CHECK(reserved.front() == 29);
}

void testFailedFrameArrayCopyPreservesDestination()
{
    sokoban::FrameArray<ThrowingCopy> source;
    CHECK(source.push_back(ThrowingCopy(99)));
    sokoban::FrameArena arena("copy test", sokoban::arenaBytesFor<ThrowingCopy>(2));
    sokoban::FrameArray<ThrowingCopy> destination(arena, 2);
    CHECK(destination.push_back(ThrowingCopy(7)));
    const auto* originalData = destination.data();

    bool threw = false;
    ThrowingCopy::fail = true;
    try {
        destination = source;
    } catch (const std::runtime_error&) {
        threw = true;
    }
    ThrowingCopy::fail = false;
    CHECK(threw);
    CHECK(destination.arenaBacked());
    CHECK(destination.data() == originalData);
    CHECK(destination.size() == 1);
    CHECK(destination.capacity() == 2);
    CHECK(destination.front().value == 7);

    destination = source;
    CHECK(!destination.arenaBacked());
    CHECK(destination.size() == 1);
    CHECK(destination.front().value == 99);
}

void testRenderFrameTakesAllCollectionStorageInOneArena()
{
    sokoban::FrameArena arena(
        "render test", sokoban::renderFrameArenaBytes());
    sokoban::RenderFrameData frame(arena);
    CHECK(frame.tiles.arenaBacked());
    CHECK(frame.waterSurfaces.arenaBacked());
    CHECK(frame.isoFaces.arenaBacked());
    CHECK(frame.particles.arenaBacked());
    CHECK(arena.bytesUsed() > 0);
    CHECK(!arena.exhausted());

    sokoban::RenderFrameData copy = frame;
    CHECK(!copy.tiles.arenaBacked());
    CHECK(!copy.waterSurfaces.arenaBacked());
    CHECK(!copy.isoFaces.arenaBacked());
    CHECK(!copy.particles.arenaBacked());
}

} // namespace

int main()
{
    testArenaMemoryTracksOwnersRatherThanFrameUsage();
    testConcurrentArenaOwnersPublishCoherentMemorySnapshots();
    testAlignmentExhaustionWarningReportsZeroUsableBytes();
    testBumpsAndReports();
    testAlignmentPadding();
    testResetReturnsTheWholeArena();
    testExhaustionIsReportedNotFatal();
    testAllocateUninitializedRespectsType();
    testArenaArrayFillsAndDropsRatherThanGrowing();
    testArenaArrayFromExhaustedArenaIsEmptyNotNull();
    testFrameArrayPreservesOwningCopySemantics();
    testArenaBackedFrameArrayKeepsItsFixedCapacity();
    testEmptyFrameArraysRemainWritableThroughCopiesAndMoves();
    testFailedFirstFrameArrayWriteLeavesUsableStorage();
    testFailedFrameArrayCopyPreservesDestination();
    testRenderFrameTakesAllCollectionStorageInOneArena();

    if (failures > 0) {
        std::cerr << "FrameArenaTests: " << failures << " failure(s) of "
                  << checks << " checks\n";
        return 1;
    }
    std::cout << "FrameArenaTests: " << checks << " checks passed\n";
    return 0;
}
