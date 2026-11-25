#include "TestHarness.hpp"

#include "engine/render/AssetLoadScheduler.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

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

void testUpcomingWorkRunsBetweenVisibleAndPrefetch()
{
    TEST("upcomingWorkRunsBetweenVisibleAndPrefetch");
    AssetLoadScheduler scheduler({
        .maxConcurrentCpuJobs = 1,
        .preparedAssetBytes = 100,
    });
    // Requested in the reverse of the order they must start in.
    scheduler.request(texture(3), AssetLoadPriority::Prefetch, 10);
    scheduler.request(model(2), AssetLoadPriority::Upcoming, 10);
    scheduler.request(model(1), AssetLoadPriority::Visible, 10);

    CHECK(scheduler.beginNext(0) == model(1));
    scheduler.complete(model(1));
    CHECK(scheduler.beginNext(0) == model(2));
    scheduler.complete(model(2));
    CHECK(scheduler.beginNext(0) == texture(3));
}

void testUpcomingWorkSurvivesPrefetchCancellationAndCanBePromoted()
{
    TEST("upcomingWorkSurvivesPrefetchCancellationAndCanBePromoted");
    AssetLoadScheduler scheduler({
        .maxConcurrentCpuJobs = 1,
        .preparedAssetBytes = 100,
    });
    scheduler.request(model(1), AssetLoadPriority::Upcoming, 10);
    scheduler.request(model(2), AssetLoadPriority::Upcoming, 10);
    scheduler.request(texture(3), AssetLoadPriority::Prefetch, 10);

    const std::vector<AssetLoadKey> cancelled =
        scheduler.cancelQueuedPrefetches();
    CHECK(cancelled.size() == 1);
    CHECK(cancelled.front() == texture(3));
    CHECK(scheduler.queuedCount() == 2);

    // A frame that starts drawing the later one moves it ahead; a weaker
    // request afterwards does not move it back.
    scheduler.request(model(2), AssetLoadPriority::Visible, 10);
    scheduler.request(model(2), AssetLoadPriority::Prefetch, 10);
    CHECK(scheduler.beginNext(0) == model(2));
    scheduler.complete(model(2));
    CHECK(scheduler.beginNext(0) == model(1));
}

void testWorldChangeCancelsUpcomingWorkButKeepsVisibleAndActive()
{
    TEST("worldChangeCancelsUpcomingWorkButKeepsVisibleAndActive");
    AssetLoadScheduler scheduler({
        .maxConcurrentCpuJobs = 1,
        .preparedAssetBytes = 100,
    });
    scheduler.request(model(1), AssetLoadPriority::Upcoming, 10);
    CHECK(scheduler.beginNext(0) == model(1));
    scheduler.request(model(2), AssetLoadPriority::Upcoming, 10);
    scheduler.request(texture(3), AssetLoadPriority::Prefetch, 10);
    scheduler.request(model(4), AssetLoadPriority::Visible, 10);

    std::vector<AssetLoadKey> cancelled =
        scheduler.cancelQueuedBackgroundWork();
    CHECK(cancelled.size() == 2);
    CHECK(std::ranges::find(cancelled, model(2)) != cancelled.end());
    CHECK(std::ranges::find(cancelled, texture(3)) != cancelled.end());
    // The running job finishes; the frame's own request is untouched.
    CHECK(scheduler.activeCount() == 1);
    CHECK(scheduler.queuedCount() == 1);
    CHECK(scheduler.cancelledPrefetchCount() == 2);
    scheduler.complete(model(1));
    CHECK(scheduler.beginNext(0) == model(4));
    scheduler.complete(model(4));
    CHECK(!scheduler.beginNext(0).has_value());
}

void testCpuJobsScaleWithSpareWorkers()
{
    TEST("cpuJobsScaleWithSpareWorkers");
    // Too few workers to spare any: the two jobs the scheduler always ran.
    CHECK(assetCpuJobsForWorkers(0) == 2);
    CHECK(assetCpuJobsForWorkers(1) == 2);
    CHECK(assetCpuJobsForWorkers(3) == 2);
    CHECK(assetCpuJobsForWorkers(4) == 2);
    // Two workers stay free for the frame's own parallel loops.
    CHECK(assetCpuJobsForWorkers(5) == 3);
    CHECK(assetCpuJobsForWorkers(7) == 5);
    CHECK(assetCpuJobsForWorkers(10) == 8);
    CHECK(assetCpuJobsForWorkers(15) == 8);
    CHECK(assetCpuJobsForWorkers(63) == 8);
    static_assert(assetCpuJobsForWorkers(15) == 8);
}

void testPublicationTimeBudgetsMayBeDisabled()
{
    TEST("publicationTimeBudgetsMayBeDisabled");
    // Zero means "no time limit", unlike the count budgets, where zero would
    // stop loading altogether.
    const AssetLoadScheduler scheduler({
        .visiblePublicationMicroseconds = 0,
        .backgroundPublicationMicroseconds = 0,
    });
    CHECK(scheduler.budget().visiblePublicationMicroseconds == 0);
    CHECK(scheduler.budget().backgroundPublicationMicroseconds == 0);
    // The shipped defaults let a frame publish more than one asset, and give
    // the picture on screen more time than background work.
    const AssetLoadingBudget defaults;
    CHECK(defaults.maxPublicationsPerFrame > 1);
    CHECK(defaults.visiblePublicationMicroseconds >
        defaults.backgroundPublicationMicroseconds);
    CHECK(defaults.backgroundPublicationMicroseconds > 0);
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
    testUpcomingWorkRunsBetweenVisibleAndPrefetch();
    testUpcomingWorkSurvivesPrefetchCancellationAndCanBePromoted();
    testWorldChangeCancelsUpcomingWorkButKeepsVisibleAndActive();
    testCpuJobsScaleWithSpareWorkers();
    testPublicationTimeBudgetsMayBeDisabled();
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
