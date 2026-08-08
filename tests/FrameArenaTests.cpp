#include "engine/ArenaArray.hpp"
#include "engine/FrameArena.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

int failures = 0;
int checks = 0;

void checkImpl(bool condition, const char* expression, int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)

[[nodiscard]] bool isAlignedTo(const void* pointer, std::size_t alignment)
{
    return reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0;
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

} // namespace

int main()
{
    testBumpsAndReports();
    testAlignmentPadding();
    testResetReturnsTheWholeArena();
    testExhaustionIsReportedNotFatal();
    testAllocateUninitializedRespectsType();
    testArenaArrayFillsAndDropsRatherThanGrowing();
    testArenaArrayFromExhaustedArenaIsEmptyNotNull();

    if (failures > 0) {
        std::cerr << "FrameArenaTests: " << failures << " failure(s) of "
                  << checks << " checks\n";
        return 1;
    }
    std::cout << "FrameArenaTests: " << checks << " checks passed\n";
    return 0;
}
