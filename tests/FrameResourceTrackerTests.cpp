#include "TestHarness.hpp"

#include "engine/render/FrameResourceTracker.hpp"
#include "engine/render/FrameRetirementQueue.hpp"
#include "engine/render/ReusableScratchPool.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

template <typename Exception, typename Function>
void checkThrows(Function function, int line)
{
    ++checks;
    try {
        function();
    } catch (const Exception&) {
        return;
    } catch (...) {
    }
    ++failures;
    std::cerr << "FAIL line " << line
              << ": expected exception\n";
}

#define CHECK_THROWS(type, expression) \
    checkThrows<type>([&] { expression; }, __LINE__)

void testTracksOverlappingGenerationsExactly()
{
    sokoban::FrameResourceTracker tracker(2);
    tracker.markSubmitted(0, 10);
    tracker.markSubmitted(1, 10);
    CHECK(tracker.pendingMask() == 0b11);
    CHECK(tracker.pendingMaskForGeneration(10) == 0b11);

    CHECK(tracker.complete(0));
    tracker.markSubmitted(0, 11);
    CHECK(tracker.pendingMask() == 0b11);
    CHECK(tracker.pendingMaskForGeneration(10) == 0b10);
    CHECK(tracker.pendingMaskForGeneration(11) == 0b01);

    CHECK(tracker.complete(1));
    CHECK(tracker.pendingMask() == 0b01);
    CHECK(!tracker.complete(1));
    CHECK(tracker.complete(0));
    CHECK(tracker.pendingMask() == 0);
}

void testRejectsInvalidSubmissionTransitions()
{
    sokoban::FrameResourceTracker tracker(2);
    CHECK_THROWS(std::invalid_argument,
        tracker.markSubmitted(0, 0));
    tracker.markSubmitted(0, 1);
    CHECK_THROWS(std::logic_error,
        tracker.markSubmitted(0, 2));
    CHECK_THROWS(std::out_of_range,
        tracker.markSubmitted(2, 1));
}

void testScratchStorageIsNeverReusedWhileLeased()
{
    struct Scratch {
        int value = 0;
    };
    sokoban::ReusableScratchPool<Scratch, 2> pool;

    auto first = pool.acquire();
    auto second = pool.acquire();
    first->value = 11;
    second->value = 22;
    Scratch* firstAddress = first.get();

    auto overflow = pool.acquire();
    CHECK(overflow.get() != first.get());
    CHECK(overflow.get() != second.get());
    overflow->value = 33;
    CHECK(first->value == 11);
    CHECK(second->value == 22);

    first.reset();
    auto reused = pool.acquire();
    CHECK(reused.get() == firstAddress);
    CHECK(second->value == 22);
}

void testRetirementWaitsForEveryReferencingFrame()
{
    sokoban::FrameRetirementQueue<int> queue;
    std::vector<int> destroyed;
    const auto drain = [&] {
        queue.drainCompleted([&](int resource) {
            destroyed.push_back(resource);
        });
    };

    queue.retire(10, 0b11);
    drain();
    CHECK(destroyed.empty());

    queue.completeFrame(0);
    drain();
    CHECK(destroyed.empty());

    // This resource was retired after frame zero completed, so only the
    // still-pending frame owns it. A later frame-zero submission must not be
    // retroactively attached to either retirement.
    queue.retire(20, 0b10);
    queue.completeFrame(1);
    drain();
    CHECK(destroyed.size() == 2);
    CHECK(destroyed[0] == 10);
    CHECK(destroyed[1] == 20);
    CHECK(queue.empty());
}

void testRetirementWithoutPendingFramesIsImmediate()
{
    sokoban::FrameRetirementQueue<int> queue;
    int destroyed = 0;
    queue.retire(7, 0);
    queue.drainCompleted([&](int resource) { destroyed = resource; });
    CHECK(destroyed == 7);
    CHECK(queue.empty());
}

void testDrainPreservesPendingEntriesBetweenCompletedOnes()
{
    sokoban::FrameRetirementQueue<int> queue;
    std::vector<int> destroyed;
    queue.retire(10, 0);
    queue.retire(20, 0b10);
    queue.retire(30, 0);
    const auto destroy = [&](int resource) { destroyed.push_back(resource); };
    queue.drainCompleted(destroy);
    CHECK((destroyed == std::vector<int> { 10, 30 }));
    CHECK(queue.size() == 1);
    queue.completeFrame(1);
    queue.drainCompleted(destroy);
    CHECK((destroyed == std::vector<int> { 10, 30, 20 }));
    CHECK(queue.empty());
}

void testDrainMovesOwnedResourcesWithoutLosingPendingEntries()
{
    sokoban::FrameRetirementQueue<std::unique_ptr<int>> queue;
    std::vector<int> destroyed;
    queue.retire(std::make_unique<int>(10), 0b01);
    queue.retire(std::make_unique<int>(20), 0);
    queue.retire(std::make_unique<int>(30), 0);
    queue.retire(std::make_unique<int>(40), 0b10);
    const auto destroy = [&](std::unique_ptr<int>& resource) {
        CHECK(resource != nullptr);
        if (resource) {
            destroyed.push_back(*resource);
            resource.reset();
        }
    };

    queue.drainCompleted(destroy);
    CHECK((destroyed == std::vector<int> { 20, 30 }));
    CHECK(queue.size() == 2);
    queue.completeFrame(0);
    queue.drainCompleted(destroy);
    CHECK((destroyed == std::vector<int> { 20, 30, 10 }));
    CHECK(queue.size() == 1);
    queue.completeFrame(1);
    queue.drainCompleted(destroy);
    CHECK((destroyed == std::vector<int> { 20, 30, 10, 40 }));
    CHECK(queue.empty());
}

void testDrainRemovesEarlierEntriesWhenDestroyThrows()
{
    sokoban::FrameRetirementQueue<int> queue;
    std::vector<int> destroyed;
    queue.retire(10, 0);
    queue.retire(20, 0);
    queue.retire(30, 0b10);
    CHECK_THROWS(std::runtime_error, queue.drainCompleted([&](int resource) {
        if (resource == 20) {
            throw std::runtime_error("injected destroy failure");
        }
        destroyed.push_back(resource);
    }));
    CHECK((destroyed == std::vector<int> { 10 }));
    CHECK(queue.size() == 2);

    const auto destroy = [&](int resource) { destroyed.push_back(resource); };
    queue.drainCompleted(destroy);
    CHECK((destroyed == std::vector<int> { 10, 20 }));
    CHECK(queue.size() == 1);
    queue.completeFrame(1);
    queue.drainCompleted(destroy);
    CHECK((destroyed == std::vector<int> { 10, 20, 30 }));
    CHECK(queue.empty());
}

} // namespace

int main()
{
    testTracksOverlappingGenerationsExactly();
    testRejectsInvalidSubmissionTransitions();
    testScratchStorageIsNeverReusedWhileLeased();
    testRetirementWaitsForEveryReferencingFrame();
    testRetirementWithoutPendingFramesIsImmediate();
    testDrainPreservesPendingEntriesBetweenCompletedOnes();
    testDrainMovesOwnedResourcesWithoutLosingPendingEntries();
    testDrainRemovesEarlierEntriesWhenDestroyThrows();

    if (failures == 0) {
        std::cout << "FrameResourceTrackerTests: "
                  << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "FrameResourceTrackerTests: "
              << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
