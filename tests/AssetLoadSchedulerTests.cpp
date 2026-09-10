#include "TestHarness.hpp"

#include "engine/render/AssetLoadScheduler.hpp"

#include <iostream>
#include <stdexcept>

namespace {

using namespace sokoban;

constexpr AssetLoadKey model(uint32_t index)
{
    return { AssetLoadKind::Model, index };
}

constexpr AssetLoadKey texture(uint32_t index)
{
    return { AssetLoadKind::Texture, index };
}

void testVisibleWorkPreemptsPrefetch()
{
    TEST("visibleWorkPreemptsPrefetch");
    AssetLoadScheduler scheduler({
        .maxConcurrentCpuJobs = 1,
        .preparedAssetBytes = 100,
    });
    scheduler.request(texture(3), AssetLoadPriority::Prefetch, 10);
    scheduler.request(model(1), AssetLoadPriority::Visible, 10);

    CHECK(scheduler.beginNext(0) == model(1));
    scheduler.complete(model(1));
    CHECK(scheduler.beginNext(0) == texture(3));
}

void testQueuedPrefetchesAreCancelledButActiveWorkSurvives()
{
    TEST("queuedPrefetchesAreCancelledButActiveWorkSurvives");
    AssetLoadScheduler scheduler({
        .maxConcurrentCpuJobs = 1,
        .preparedAssetBytes = 100,
    });
    scheduler.request(model(1), AssetLoadPriority::Prefetch, 10);
    scheduler.request(texture(2), AssetLoadPriority::Prefetch, 10);
    CHECK(scheduler.beginNext(0) == model(1));

    const std::vector<AssetLoadKey> cancelled =
        scheduler.cancelQueuedPrefetches();
    CHECK(cancelled.size() == 1);
    CHECK(cancelled.front() == texture(2));
    CHECK(scheduler.activeCount() == 1);
    CHECK(scheduler.queuedCount() == 0);
    CHECK(scheduler.cancelledPrefetchCount() == 1);

    scheduler.complete(model(1));
    CHECK(!scheduler.beginNext(0).has_value());
}

void testRerequestRaisesPriorityWithoutDuplicatingWork()
{
    TEST("rerequestRaisesPriorityWithoutDuplicatingWork");
    AssetLoadScheduler scheduler({
        .maxConcurrentCpuJobs = 1,
        .preparedAssetBytes = 100,
    });
    scheduler.request(texture(1), AssetLoadPriority::Prefetch, 10);
    scheduler.request(model(2), AssetLoadPriority::Visible, 10);
    scheduler.request(texture(1), AssetLoadPriority::Visible, 10);

    CHECK(scheduler.queuedCount() == 2);
    CHECK(scheduler.beginNext(0) == texture(1));
    scheduler.complete(texture(1));
    CHECK(scheduler.beginNext(0) == model(2));
}

void testCpuBudgetBoundsActiveJobs()
{
    TEST("cpuBudgetBoundsActiveJobs");
    AssetLoadScheduler scheduler({
        .maxConcurrentCpuJobs = 2,
        .preparedAssetBytes = 100,
    });
    scheduler.request(model(1), AssetLoadPriority::Visible, 20);
    scheduler.request(model(2), AssetLoadPriority::Visible, 20);
    scheduler.request(model(3), AssetLoadPriority::Visible, 20);

    CHECK(scheduler.beginNext(0).has_value());
    CHECK(scheduler.beginNext(0).has_value());
    CHECK(!scheduler.beginNext(0).has_value());
    CHECK(scheduler.activeCount() == 2);
}

void testPreparedBudgetPausesAndResumesDecode()
{
    TEST("preparedBudgetPausesAndResumesDecode");
    AssetLoadScheduler scheduler({
        .maxConcurrentCpuJobs = 2,
        .preparedAssetBytes = 100,
    });
    scheduler.request(model(1), AssetLoadPriority::Visible, 60);
    scheduler.request(model(2), AssetLoadPriority::Visible, 50);

    CHECK(scheduler.beginNext(0) == model(1));
    CHECK(scheduler.activePreparedBytes() == 60);
    CHECK(!scheduler.beginNext(0).has_value());
    CHECK(scheduler.preparedBudgetDeferrals() == 1);
    scheduler.complete(model(1));
    CHECK(scheduler.activePreparedBytes() == 0);
    CHECK(!scheduler.beginNext(60).has_value());
    CHECK(scheduler.beginNext(0) == model(2));
}

void testOversizedAndUnknownAssetsRunAlone()
{
    TEST("oversizedAndUnknownAssetsRunAlone");
    AssetLoadScheduler scheduler({
        .maxConcurrentCpuJobs = 2,
        .preparedAssetBytes = 100,
    });
    scheduler.request(model(1), AssetLoadPriority::Visible, 120);
    scheduler.request(texture(1), AssetLoadPriority::Visible, 0);

    CHECK(scheduler.beginNext(0) == model(1));
    CHECK(scheduler.oversizedAssetStarts() == 1);
    CHECK(!scheduler.beginNext(0).has_value());
    scheduler.complete(model(1));
    CHECK(!scheduler.beginNext(120).has_value());
    CHECK(scheduler.beginNext(0) == texture(1));
    CHECK(scheduler.activePreparedBytes() == 100);
}

void testInvalidBudgetsAreRejected()
{
    TEST("invalidBudgetsAreRejected");
    bool cpuRejected = false;
    try {
        (void)AssetLoadScheduler({ .maxConcurrentCpuJobs = 0 });
    } catch (const std::invalid_argument&) {
        cpuRejected = true;
    }
    CHECK(cpuRejected);

    bool publicationRejected = false;
    try {
        (void)AssetLoadScheduler({ .maxPublicationsPerFrame = 0 });
    } catch (const std::invalid_argument&) {
        publicationRejected = true;
    }
    CHECK(publicationRejected);

    bool preparedRejected = false;
    try {
        (void)AssetLoadScheduler({ .preparedAssetBytes = 0 });
    } catch (const std::invalid_argument&) {
        preparedRejected = true;
    }
    CHECK(preparedRejected);
}

} // namespace

int main()
{
    testVisibleWorkPreemptsPrefetch();
    testQueuedPrefetchesAreCancelledButActiveWorkSurvives();
    testRerequestRaisesPriorityWithoutDuplicatingWork();
    testCpuBudgetBoundsActiveJobs();
    testPreparedBudgetPausesAndResumesDecode();
    testOversizedAndUnknownAssetsRunAlone();
    testInvalidBudgetsAreRejected();

    if (failures == 0) {
        std::cout << "AssetLoadSchedulerTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "AssetLoadSchedulerTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
