// Covers the material-buffer range allocator extracted from
// VulkanModelResources, which had no test of its own: it was a private method
// pair operating on private state behind a Vulkan device.
//
// The cases are written against the behaviour it already had. Where that
// behaviour is surprising - a double release being absorbed, fragmentation
// refusing an allocation the buffer has room for - there is a test saying so
// rather than a quiet correction.

#include "TestHarness.hpp"

#include "engine/render/MaterialRangeAllocator.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

namespace {

using namespace sokoban;

constexpr uint32_t capacity = 1024;

using Ranges = std::vector<MaterialRangeAllocator::FreeRange>;

MaterialRangeAllocator seeded()
{
    MaterialRangeAllocator allocator;
    // One reserved entry, matching the buffer's white fallback at index zero.
    allocator.reset(1);
    return allocator;
}

void testEntryZeroIsNeverHandedOut()
{
    TEST("entryZeroIsNeverHandedOut");
    MaterialRangeAllocator allocator = seeded();
    CHECK(allocator.highWaterMark() == 1);
    CHECK(allocator.allocate(1, capacity) == std::optional<uint32_t> { 1 });

    // Without the reservation the first allocation would land on zero, which
    // the buffer treats as "this model has published nothing".
    MaterialRangeAllocator unseeded;
    unseeded.reset(0);
    CHECK(!unseeded.allocate(1, capacity).has_value());
}

void testAllocationsBumpUpwards()
{
    TEST("allocationsBumpUpwards");
    MaterialRangeAllocator allocator = seeded();
    CHECK(allocator.allocate(3, capacity) == std::optional<uint32_t> { 1 });
    CHECK(allocator.allocate(2, capacity) == std::optional<uint32_t> { 4 });
    CHECK(allocator.allocate(1, capacity) == std::optional<uint32_t> { 6 });
    CHECK(allocator.highWaterMark() == 7);
    CHECK(allocator.freeRanges().empty());
}

void testZeroLengthAllocationIsRefused()
{
    TEST("zeroLengthAllocationIsRefused");
    MaterialRangeAllocator allocator = seeded();
    CHECK(!allocator.allocate(0, capacity).has_value());
    CHECK(allocator.highWaterMark() == 1);
}

void testCapacityIsARealCeiling()
{
    TEST("capacityIsARealCeiling");
    MaterialRangeAllocator allocator = seeded();
    // Room for capacity - 1 entries above the reserved one.
    CHECK(allocator.allocate(capacity - 1, capacity)
        == std::optional<uint32_t> { 1 });
    CHECK(allocator.highWaterMark() == capacity);
    CHECK(!allocator.allocate(1, capacity).has_value());

    MaterialRangeAllocator fresh = seeded();
    CHECK(!fresh.allocate(capacity, capacity).has_value());
    // A refused allocation leaves nothing behind.
    CHECK(fresh.highWaterMark() == 1);
}

void testReleasedRunsAreReused()
{
    TEST("releasedRunsAreReused");
    MaterialRangeAllocator allocator = seeded();
    const uint32_t first = *allocator.allocate(4, capacity);   // 1..4
    const uint32_t second = *allocator.allocate(4, capacity);  // 5..8
    CHECK(first == 1 && second == 5);

    allocator.release(first, 4);
    CHECK(allocator.freeEntryCount() == 4);
    // Reuse rather than growing.
    CHECK(allocator.allocate(4, capacity) == std::optional<uint32_t> { 1 });
    CHECK(allocator.highWaterMark() == 9);
    CHECK(allocator.freeRanges().empty());
}

void testPartialReuseLeavesTheTail()
{
    TEST("partialReuseLeavesTheTail");
    MaterialRangeAllocator allocator = seeded();
    const uint32_t run = *allocator.allocate(6, capacity); // 1..6
    allocator.release(run, 6);

    // Taken from the front; entries 3..6 stay free.
    CHECK(allocator.allocate(2, capacity) == std::optional<uint32_t> { 1 });
    CHECK(allocator.freeRanges() == (Ranges { { 3, 4 } }));
    CHECK(allocator.allocate(4, capacity) == std::optional<uint32_t> { 3 });
    CHECK(allocator.freeRanges().empty());
}

void testAdjacentReleasesCoalesce()
{
    TEST("adjacentReleasesCoalesce");
    MaterialRangeAllocator allocator = seeded();
    const uint32_t a = *allocator.allocate(2, capacity); // 1..2
    const uint32_t b = *allocator.allocate(2, capacity); // 3..4
    const uint32_t c = *allocator.allocate(2, capacity); // 5..6

    // Released out of order, and the middle one last, so the merge has to join
    // a run on each side rather than only extending one.
    allocator.release(a, 2);
    allocator.release(c, 2);
    CHECK(allocator.freeRanges() == (Ranges { { 1, 2 }, { 5, 2 } }));
    allocator.release(b, 2);
    CHECK(allocator.freeRanges() == (Ranges { { 1, 6 } }));

    // And the coalesced run is usable as one piece.
    CHECK(allocator.allocate(6, capacity) == std::optional<uint32_t> { 1 });
}

void testNonAdjacentReleasesStaySeparate()
{
    TEST("nonAdjacentReleasesStaySeparate");
    MaterialRangeAllocator allocator = seeded();
    const uint32_t a = *allocator.allocate(2, capacity); // 1..2
    (void)allocator.allocate(2, capacity);               // 3..4 stays live
    const uint32_t c = *allocator.allocate(2, capacity); // 5..6
    allocator.release(a, 2);
    allocator.release(c, 2);
    CHECK(allocator.freeRanges() == (Ranges { { 1, 2 }, { 5, 2 } }));
    CHECK(allocator.freeEntryCount() == 4);
    // Neither hole fits a run of three, so it goes above the high-water mark.
    CHECK(allocator.allocate(3, capacity) == std::optional<uint32_t> { 7 });
}

void testReleaseIgnoresTheFallbackAndEmptyRuns()
{
    TEST("releaseIgnoresTheFallbackAndEmptyRuns");
    MaterialRangeAllocator allocator = seeded();
    (void)allocator.allocate(2, capacity);
    allocator.release(0, 4);  // entry zero is not owned by anyone
    allocator.release(1, 0);  // a model with no materials never claimed a run
    CHECK(allocator.freeRanges().empty());
    CHECK(allocator.freeEntryCount() == 0);
}

void testFragmentationCanRefuseAnAllocationThatWouldFit()
{
    TEST("fragmentationCanRefuseAnAllocationThatWouldFit");
    // The high-water mark never walks back down, so a buffer whose free space
    // is split into small holes can refuse a run that the total free space
    // could hold. Pinned because it is the failure mode behind
    // "Material buffer is exhausted" appearing while the buffer looks empty.
    MaterialRangeAllocator allocator = seeded();
    const uint32_t small = 4;
    std::vector<uint32_t> runs;
    while (true) {
        const std::optional<uint32_t> base = allocator.allocate(small, capacity);
        if (!base) {
            break;
        }
        runs.push_back(*base);
    }
    // Free every other run, leaving alternating four-entry holes.
    for (std::size_t index = 0; index < runs.size(); index += 2) {
        allocator.release(runs[index], small);
    }
    CHECK(allocator.freeEntryCount() > small * 2);
    // Plenty free in total, but no single hole is big enough.
    CHECK(!allocator.allocate(small * 2, capacity).has_value());
    CHECK(allocator.allocate(small, capacity).has_value());
}

void testDoubleReleaseIsAbsorbed()
{
    TEST("doubleReleaseIsAbsorbed");
    // Documented, not endorsed. Releasing the same run twice merges the
    // duplicate into itself and reports nothing, so this cannot be relied on to
    // catch a caller that loses track of a range. Nothing in the renderer does
    // that today: a slot's base is zeroed as it is released.
    MaterialRangeAllocator allocator = seeded();
    const uint32_t run = *allocator.allocate(4, capacity);
    allocator.release(run, 4);
    allocator.release(run, 4);
    CHECK(allocator.freeRanges() == (Ranges { { 1, 4 } }));
    CHECK(allocator.freeEntryCount() == 4);
}

void testOverlappingReleaseIsAbsorbed()
{
    TEST("overlappingReleaseIsAbsorbed");
    MaterialRangeAllocator allocator = seeded();
    (void)allocator.allocate(8, capacity); // 1..8
    allocator.release(1, 4);
    allocator.release(3, 4); // overlaps the first by two entries
    CHECK(allocator.freeRanges() == (Ranges { { 1, 6 } }));
}

void testResetClearsEverything()
{
    TEST("resetClearsEverything");
    MaterialRangeAllocator allocator = seeded();
    const uint32_t run = *allocator.allocate(4, capacity);
    allocator.release(run, 4);
    CHECK(!allocator.freeRanges().empty());

    allocator.reset(0);
    CHECK(allocator.highWaterMark() == 0);
    CHECK(allocator.freeRanges().empty());
    CHECK(allocator.freeEntryCount() == 0);
}

void testRangesStaySortedUnderScatteredReleases()
{
    TEST("rangesStaySortedUnderScatteredReleases");
    // The free list being sorted is an invariant release() maintains and
    // allocate() quietly relies on, so exercise it out of order.
    MaterialRangeAllocator allocator = seeded();
    std::vector<uint32_t> runs;
    for (int index = 0; index < 8; ++index) {
        runs.push_back(*allocator.allocate(2, capacity));
    }
    for (const std::size_t index : { 5u, 1u, 7u, 3u }) {
        allocator.release(runs[index], 2);
    }
    const auto& ranges = allocator.freeRanges();
    CHECK(ranges.size() == 4);
    for (std::size_t index = 1; index < ranges.size(); ++index) {
        CHECK(ranges[index - 1].base < ranges[index].base);
        // Sorted and never touching, or they would have merged.
        CHECK(ranges[index - 1].base + ranges[index - 1].count
            < ranges[index].base);
    }
}

} // namespace

int main()
{
    testEntryZeroIsNeverHandedOut();
    testAllocationsBumpUpwards();
    testZeroLengthAllocationIsRefused();
    testCapacityIsARealCeiling();

    testReleasedRunsAreReused();
    testPartialReuseLeavesTheTail();
    testAdjacentReleasesCoalesce();
    testNonAdjacentReleasesStaySeparate();
    testReleaseIgnoresTheFallbackAndEmptyRuns();

    testFragmentationCanRefuseAnAllocationThatWouldFit();
    testDoubleReleaseIsAbsorbed();
    testOverlappingReleaseIsAbsorbed();
    testResetClearsEverything();
    testRangesStaySortedUnderScatteredReleases();

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "material_range_allocator: " << checks << " checks passed\n";
    return 0;
}
