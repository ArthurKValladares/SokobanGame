#include "engine/render/AssetLoadScheduler.hpp"

#include <iostream>
#include <stdexcept>

namespace {

using namespace sokoban;

int failures = 0;
int checks = 0;
const char* currentTest = "";

void checkImpl(bool ok, const char* expression, int line)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << "FAIL [" << currentTest << "] line " << line
                  << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)
#define TEST(name) currentTest = name

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
    AssetLoadScheduler scheduler({ .maxConcurrentCpuJobs = 1 });
    scheduler.request(texture(3), AssetLoadPriority::Prefetch);
    scheduler.request(model(1), AssetLoadPriority::Visible);

    CHECK(scheduler.beginNext() == model(1));
    scheduler.complete(model(1));
    CHECK(scheduler.beginNext() == texture(3));
}

void testQueuedPrefetchesAreCancelledButActiveWorkSurvives()
{
    TEST("queuedPrefetchesAreCancelledButActiveWorkSurvives");
    AssetLoadScheduler scheduler({ .maxConcurrentCpuJobs = 1 });
    scheduler.request(model(1), AssetLoadPriority::Prefetch);
    scheduler.request(texture(2), AssetLoadPriority::Prefetch);
    CHECK(scheduler.beginNext() == model(1));

    const std::vector<AssetLoadKey> cancelled =
        scheduler.cancelQueuedPrefetches();
    CHECK(cancelled.size() == 1);
    CHECK(cancelled.front() == texture(2));
    CHECK(scheduler.activeCount() == 1);
    CHECK(scheduler.queuedCount() == 0);
    CHECK(scheduler.cancelledPrefetchCount() == 1);

    scheduler.complete(model(1));
    CHECK(!scheduler.beginNext().has_value());
}

void testRerequestRaisesPriorityWithoutDuplicatingWork()
{
    TEST("rerequestRaisesPriorityWithoutDuplicatingWork");
    AssetLoadScheduler scheduler({ .maxConcurrentCpuJobs = 1 });
    scheduler.request(texture(1), AssetLoadPriority::Prefetch);
    scheduler.request(model(2), AssetLoadPriority::Visible);
    scheduler.request(texture(1), AssetLoadPriority::Visible);

    CHECK(scheduler.queuedCount() == 2);
    CHECK(scheduler.beginNext() == texture(1));
    scheduler.complete(texture(1));
    CHECK(scheduler.beginNext() == model(2));
}

void testCpuBudgetBoundsActiveJobs()
{
    TEST("cpuBudgetBoundsActiveJobs");
    AssetLoadScheduler scheduler({ .maxConcurrentCpuJobs = 2 });
    scheduler.request(model(1), AssetLoadPriority::Visible);
    scheduler.request(model(2), AssetLoadPriority::Visible);
    scheduler.request(model(3), AssetLoadPriority::Visible);

    CHECK(scheduler.beginNext().has_value());
    CHECK(scheduler.beginNext().has_value());
    CHECK(!scheduler.beginNext().has_value());
    CHECK(scheduler.activeCount() == 2);
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
}

} // namespace

int main()
{
    testVisibleWorkPreemptsPrefetch();
    testQueuedPrefetchesAreCancelledButActiveWorkSurvives();
    testRerequestRaisesPriorityWithoutDuplicatingWork();
    testCpuBudgetBoundsActiveJobs();
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
