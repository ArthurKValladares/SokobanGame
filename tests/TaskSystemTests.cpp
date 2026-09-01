// Headless tests for the engine task system: standard library only.

#include "TestHarness.hpp"

#include "engine/TaskSystem.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {

void testEnqueueReturnsValues()
{
    TEST("enqueueReturnsValues");
    sokoban::TaskSystem system(4);

    std::vector<std::future<int>> futures;
    for (int i = 0; i < 100; ++i) {
        futures.push_back(system.enqueue([i] { return i * i; }));
    }
    long long total = 0;
    for (auto& future : futures) {
        total += future.get();
    }
    // sum of squares 0..99
    CHECK(total == 328350);
    CHECK(system.workerCount() == 4);
    const auto counterDeadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(1);
    while (system.executedTaskCount() != 100 &&
        std::chrono::steady_clock::now() < counterDeadline) {
        std::this_thread::yield();
    }
    CHECK(system.executedTaskCount() == 100);
}

void testEnqueuePropagatesExceptions()
{
    TEST("enqueuePropagatesExceptions");
    sokoban::TaskSystem system(2);
    auto future = system.enqueue([]() -> int { throw std::runtime_error("boom"); });
    bool threw = false;
    try {
        (void)future.get();
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()) == "boom";
    }
    CHECK(threw);
}

void testParallelForCoversEveryIndexOnce()
{
    TEST("parallelForCoversEveryIndexOnce");
    sokoban::TaskSystem system(4);

    const size_t count = 100000;
    std::vector<std::atomic<int>> touches(count);
    system.parallelFor(count, 64, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            touches[i].fetch_add(1, std::memory_order_relaxed);
        }
    });
    bool allOnce = true;
    for (size_t i = 0; i < count; ++i) {
        allOnce = allOnce && touches[i].load() == 1;
    }
    CHECK(allOnce);
}

void testParallelForSmallCountsRunInline()
{
    TEST("parallelForSmallCountsRunInline");
    sokoban::TaskSystem system(4);

    const std::thread::id caller = std::this_thread::get_id();
    std::atomic<bool> sameThread { false };
    system.parallelFor(8, 64, [&](size_t begin, size_t end) {
        sameThread = std::this_thread::get_id() == caller && begin == 0 && end == 8;
    });
    CHECK(sameThread.load());

    // Zero count is a no-op.
    std::atomic<int> calls { 0 };
    system.parallelFor(0, 1, [&](size_t, size_t) { calls.fetch_add(1); });
    CHECK(calls.load() == 0);
}

void testParallelForComputesDeterministicResult()
{
    TEST("parallelForComputesDeterministicResult");
    sokoban::TaskSystem system(3);

    const size_t count = 50000;
    std::vector<long long> values(count);
    system.parallelFor(count, 128, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            values[i] = static_cast<long long>(i) * 3 - 1;
        }
    });
    long long total = std::accumulate(values.begin(), values.end(), 0LL);
    // sum of (3i - 1) for i in [0, count)
    const long long n = static_cast<long long>(count);
    CHECK(total == 3 * (n * (n - 1) / 2) - n);
}

void testParallelForPropagatesInlineExceptions()
{
    TEST("parallelForPropagatesInlineExceptions");
    sokoban::TaskSystem system(2);

    bool threw = false;
    try {
        system.parallelFor(8, 64, [](size_t, size_t) {
            throw std::runtime_error("inline boom");
        });
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()) == "inline boom";
    }
    CHECK(threw);
}

void testParallelForWaitsBeforeRethrowingCallerException()
{
    TEST("parallelForWaitsBeforeRethrowingCallerException");
    sokoban::TaskSystem system(4);

    const std::thread::id caller = std::this_thread::get_id();
    std::atomic<bool> workerEntered { false };
    std::atomic<bool> callerThrowing { false };
    bool threw = false;
    try {
        system.parallelFor(1000, 1, [&](size_t, size_t) {
            if (std::this_thread::get_id() != caller) {
                workerEntered.store(true, std::memory_order_release);
                while (!callerThrowing.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                return;
            }

            while (!workerEntered.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            callerThrowing.store(true, std::memory_order_release);
            throw std::runtime_error("caller boom");
        });
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()) == "caller boom";
    }

    CHECK(threw);
    CHECK(workerEntered.load(std::memory_order_acquire));
    auto future = system.enqueue([] { return 17; });
    CHECK(future.get() == 17);
}

void testParallelForPropagatesWorkerException()
{
    TEST("parallelForPropagatesWorkerException");
    sokoban::TaskSystem system(4);

    const std::thread::id caller = std::this_thread::get_id();
    std::atomic<bool> callerEntered { false };
    std::atomic<bool> workerThrowing { false };
    bool threw = false;
    try {
        system.parallelFor(1000, 1, [&](size_t, size_t) {
            if (std::this_thread::get_id() == caller) {
                callerEntered.store(true, std::memory_order_release);
                while (!workerThrowing.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                return;
            }

            while (!callerEntered.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            workerThrowing.store(true, std::memory_order_release);
            throw std::runtime_error("worker boom");
        });
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()) == "worker boom";
    }

    CHECK(threw);
    CHECK(workerThrowing.load(std::memory_order_acquire));
    std::atomic<size_t> completed { 0 };
    system.parallelFor(128, 8, [&](size_t begin, size_t end) {
        completed.fetch_add(end - begin, std::memory_order_relaxed);
    });
    CHECK(completed.load(std::memory_order_relaxed) == 128);
}

void testGlobalInstance()
{
    TEST("globalInstance");
    CHECK(sokoban::taskSystem().workerCount() >= 1);
    auto future = sokoban::taskSystem().enqueue([] { return 42; });
    CHECK(future.get() == 42);
}

void testManyConcurrentParallelFors()
{
    TEST("manyConcurrentParallelFors");
    sokoban::TaskSystem system(4);
    // parallelFor from several enqueued tasks would violate the "tasks must
    // not block on tasks" rule, so instead hammer sequential parallelFors to
    // shake out lifetime bugs in the shared-state capture.
    for (int round = 0; round < 50; ++round) {
        std::atomic<long long> sum { 0 };
        system.parallelFor(1000, 16, [&](size_t begin, size_t end) {
            long long local = 0;
            for (size_t i = begin; i < end; ++i) {
                local += static_cast<long long>(i);
            }
            sum.fetch_add(local, std::memory_order_relaxed);
        });
        if (sum.load() != 499500) {
            CHECK(sum.load() == 499500);
            return;
        }
    }
    CHECK(true);
}

} // namespace

int main()
{
    testEnqueueReturnsValues();
    testEnqueuePropagatesExceptions();
    testParallelForCoversEveryIndexOnce();
    testParallelForSmallCountsRunInline();
    testParallelForComputesDeterministicResult();
    testParallelForPropagatesInlineExceptions();
    testParallelForWaitsBeforeRethrowingCallerException();
    testParallelForPropagatesWorkerException();
    testGlobalInstance();
    testManyConcurrentParallelFors();

    if (failures == 0) {
        std::cout << "All " << checks << " checks passed.\n";
        return 0;
    }

    std::cerr << failures << " of " << checks << " checks failed.\n";
    return 1;
}
