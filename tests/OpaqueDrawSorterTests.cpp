#include "engine/render/OpaqueDrawSorter.hpp"

#include <iostream>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

sokoban::OpaqueDrawSortKey key(
    uint32_t pipeline,
    uint32_t material,
    uint32_t mesh,
    uint32_t state = 0)
{
    sokoban::OpaqueDrawSortKey result {
        .pipeline = pipeline,
        .material = material,
        .mesh = mesh,
    };
    result.fragmentState[0] = state;
    return result;
}

void testSortAndInstanceRepeatedOpaqueDraws()
{
    std::vector<sokoban::OpaqueDrawSortItem> items {
        { .key = key(1, 0, 4), .drawIndex = 40, .instancable = true },
        { .key = key(0, 2, 7), .drawIndex = 70, .instancable = true },
        { .key = key(0, 2, 7), .drawIndex = 71, .instancable = true },
        { .key = key(0, 1, 3), .drawIndex = 30, .instancable = true },
        { .key = key(0, 2, 7, 1), .drawIndex = 72, .instancable = true },
    };
    std::vector<sokoban::OpaqueDrawBatch> batches;
    sokoban::sortOpaqueDraws(items, batches);

    check(items[0].drawIndex == 30, "sorts by material before mesh");
    check(items[1].drawIndex == 70 && items[2].drawIndex == 71,
        "keeps repeated mesh/material items adjacent");
    check(items[3].drawIndex == 72, "keeps distinct fragment state separate");
    check(items[4].drawIndex == 40, "sorts by pipeline before material and mesh");
    check(batches.size() == 4, "creates one batch for compatible repeats");
    check(batches[1].firstItem == 1 && batches[1].itemCount == 2,
        "repeated opaque work becomes one instanced batch");
}

void testSkinnedItemsNeverShareAnInstanceBatch()
{
    std::vector<sokoban::OpaqueDrawSortItem> items {
        { .key = key(0, 0, 1), .drawIndex = 1, .instancable = false },
        { .key = key(0, 0, 1), .drawIndex = 2, .instancable = false },
        { .key = key(0, 0, 1), .drawIndex = 3, .instancable = true },
    };
    std::vector<sokoban::OpaqueDrawBatch> batches;
    sokoban::sortOpaqueDraws(items, batches);

    check(batches.size() == 3, "non-instancable draws remain separate");
    check(batches[0].itemCount == 1 && batches[1].itemCount == 1 &&
            batches[2].itemCount == 1,
        "skinned work does not merge with static work");
}

void testCallerOwnedBatchStorageIsReused()
{
    std::vector<sokoban::OpaqueDrawSortItem> items {
        { .key = key(0, 0, 1), .drawIndex = 1, .instancable = true },
        { .key = key(0, 0, 1), .drawIndex = 2, .instancable = true },
    };
    std::vector<sokoban::OpaqueDrawBatch> batches;
    batches.reserve(8);
    const auto* const retainedStorage = batches.data();
    const std::size_t retainedCapacity = batches.capacity();

    sokoban::sortOpaqueDraws(items, batches);
    check(batches.data() == retainedStorage,
        "sorter retains caller-owned batch storage");
    check(batches.capacity() == retainedCapacity,
        "sorter does not shrink retained batch capacity");
    check(batches.size() == 1 && batches[0].itemCount == 2,
        "retained output still batches compatible draws");

    items.resize(1);
    items[0].instancable = false;
    sokoban::sortOpaqueDraws(items, batches);
    check(batches.data() == retainedStorage,
        "repeated sort reuses the same batch allocation");
    check(batches.size() == 1 && batches[0].itemCount == 1,
        "repeated sort replaces prior output");
}

} // namespace

int main()
{
    testSortAndInstanceRepeatedOpaqueDraws();
    testSkinnedItemsNeverShareAnInstanceBatch();
    testCallerOwnedBatchStorageIsReused();
    if (failures == 0) {
        std::cout << "OpaqueDrawSorterTests: passed\n";
        return 0;
    }
    return 1;
}
