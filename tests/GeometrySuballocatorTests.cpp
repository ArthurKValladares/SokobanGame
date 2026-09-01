#include "TestHarness.hpp"

#include "engine/render/GeometrySuballocator.hpp"

#include <iostream>
#include <stdexcept>

namespace {

using namespace sokoban;

void testAlignmentAndFirstFit()
{
    TEST("alignmentAndFirstFit");
    GeometrySuballocator allocator(128);
    const auto first = allocator.allocate(12, 16);
    const auto second = allocator.allocate(20, 32);
    CHECK(first.has_value());
    CHECK(first->offset == 0);
    CHECK(second.has_value());
    CHECK(second->offset == 32);
    CHECK(allocator.usedBytes() == 32);
    CHECK(allocator.freeBytes() == 96);
}

void testFreeRangesMerge()
{
    TEST("freeRangesMerge");
    GeometrySuballocator allocator(96);
    const auto first = allocator.allocate(32, 1);
    const auto second = allocator.allocate(32, 1);
    const auto third = allocator.allocate(32, 1);
    CHECK(first && second && third);
    allocator.release(*second);
    CHECK(allocator.largestFreeRange() == 32);
    allocator.release(*first);
    CHECK(allocator.largestFreeRange() == 64);
    allocator.release(*third);
    CHECK(allocator.usedBytes() == 0);
    CHECK(allocator.largestFreeRange() == 96);
}

void testFragmentationAndReuse()
{
    TEST("fragmentationAndReuse");
    GeometrySuballocator allocator(64);
    const auto first = allocator.allocate(16, 1);
    const auto second = allocator.allocate(16, 1);
    const auto third = allocator.allocate(16, 1);
    CHECK(first && second && third);
    allocator.release(*second);
    CHECK(!allocator.allocate(24, 1).has_value());
    const auto reused = allocator.allocate(12, 4);
    CHECK(reused.has_value());
    CHECK(reused->offset == 16);
}

void testInvalidReleasesAreRejected()
{
    TEST("invalidReleasesAreRejected");
    GeometrySuballocator allocator(32);
    const auto allocation = allocator.allocate(8, 1);
    CHECK(allocation.has_value());
    allocator.release(*allocation);
    bool doubleFreeRejected = false;
    try {
        allocator.release(*allocation);
    } catch (const std::invalid_argument&) {
        doubleFreeRejected = true;
    }
    CHECK(doubleFreeRejected);
}

} // namespace

int main()
{
    testAlignmentAndFirstFit();
    testFreeRangesMerge();
    testFragmentationAndReuse();
    testInvalidReleasesAreRejected();
    if (failures == 0) {
        std::cout << "GeometrySuballocatorTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "GeometrySuballocatorTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
