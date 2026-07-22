#include "engine/render/FrameDescriptorSync.hpp"

#include <iostream>

namespace {

int failures = 0;
int checks = 0;

void checkImpl(bool value, const char* expression, int line)
{
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)

void testEachFramePublishesNewGenerationIndependently()
{
    sokoban::FrameDescriptorSync sync(2);
    CHECK(sync.needsUpdate(0));
    CHECK(sync.needsUpdate(1));

    sync.markUpdated(0);
    CHECK(!sync.needsUpdate(0));
    CHECK(sync.needsUpdate(1));

    sync.resourcesChanged();
    CHECK(sync.needsUpdate(0));
    CHECK(sync.needsUpdate(1));

    sync.markUpdated(1);
    CHECK(sync.needsUpdate(0));
    CHECK(!sync.needsUpdate(1));
    sync.markUpdated(0);
    CHECK(!sync.needsUpdate(0));
    CHECK(!sync.needsUpdate(1));
}

void testBulkPublicationAndReset()
{
    sokoban::FrameDescriptorSync sync(3);
    sync.markAllUpdated();
    CHECK(!sync.needsUpdate(0));
    CHECK(!sync.needsUpdate(1));
    CHECK(!sync.needsUpdate(2));

    const uint64_t previousGeneration = sync.generation();
    sync.resourcesChanged();
    CHECK(sync.generation() == previousGeneration + 1);
    CHECK(sync.needsUpdate(0));
    CHECK(sync.needsUpdate(1));
    CHECK(sync.needsUpdate(2));

    sync.reset(1);
    CHECK(sync.generation() == 1);
    CHECK(sync.needsUpdate(0));
}

} // namespace

int main()
{
    testEachFramePublishesNewGenerationIndependently();
    testBulkPublicationAndReset();

    if (failures == 0) {
        std::cout << "FrameDescriptorSyncTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "FrameDescriptorSyncTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
