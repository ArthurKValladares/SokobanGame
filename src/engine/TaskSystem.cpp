#include "engine/TaskSystem.hpp"

#include <algorithm>
#include <exception>
#include <latch>
#include <memory>
#include <system_error>

namespace sokoban {
namespace {

#ifdef SOKOBAN_ENABLE_TEST_HOOKS
std::atomic<std::int64_t> workerCreationsBeforeFailureForTesting { -1 };
std::atomic<unsigned> liveWorkersForTesting { 0 };

void maybeFailWorkerCreationForTesting()
{
    std::int64_t remaining =
        workerCreationsBeforeFailureForTesting.load(std::memory_order_relaxed);
    while (remaining >= 0) {
        const std::int64_t next = remaining == 0 ? -1 : remaining - 1;
        if (workerCreationsBeforeFailureForTesting.compare_exchange_weak(
                remaining, next, std::memory_order_relaxed)) {
            if (remaining == 0) {
                throw std::system_error(
                    std::make_error_code(
                        std::errc::resource_unavailable_try_again),
                    "test-injected worker creation failure");
            }
            return;
        }
    }
}

class LiveWorkerRegistration {
public:
    LiveWorkerRegistration()
    {
        liveWorkersForTesting.fetch_add(1, std::memory_order_relaxed);
    }

    ~LiveWorkerRegistration()
    {
        liveWorkersForTesting.fetch_sub(1, std::memory_order_relaxed);
    }
};
#endif

class ParallelForState {
public:
    ParallelForState(
        size_t count,
        size_t chunkSize,
        size_t helperCount,
        const std::function<void(size_t, size_t)>& fn)
        : count_(count)
        , chunkSize_(chunkSize)
        , helpersDone_(static_cast<ptrdiff_t>(helperCount))
        , fn_(fn)
    {
    }

    void runChunks() noexcept
    {
        while (!cancelled_.load(std::memory_order_acquire)) {
            const size_t begin = nextIndex_.fetch_add(
                chunkSize_, std::memory_order_relaxed);
            if (begin >= count_) {
                return;
            }
            try {
                fn_(begin, std::min(begin + chunkSize_, count_));
            } catch (...) {
                recordFailure(std::current_exception());
                return;
            }
        }
    }

    void recordFailure(std::exception_ptr failure) noexcept
    {
        bool expected = false;
        if (failureRecorded_.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            firstFailure_ = std::move(failure);
        }
        cancelled_.store(true, std::memory_order_release);
    }

    void helperComplete() noexcept
    {
        helpersDone_.count_down();
    }

    void helpersNotQueued(size_t count) noexcept
    {
        helpersDone_.count_down(static_cast<ptrdiff_t>(count));
    }

    void waitForHelpers() const
    {
        helpersDone_.wait();
    }

    void rethrowFailure() const
    {
        if (firstFailure_) {
            std::rethrow_exception(firstFailure_);
        }
    }

private:
    const size_t count_;
    const size_t chunkSize_;
    std::atomic<size_t> nextIndex_ { 0 };
    std::atomic<bool> cancelled_ { false };
    std::atomic<bool> failureRecorded_ { false };
    std::exception_ptr firstFailure_;
    std::latch helpersDone_;
    const std::function<void(size_t, size_t)>& fn_;
};

} // namespace

TaskSystem::TaskSystem(unsigned threadCount)
{
    if (threadCount == 0) {
        const unsigned hardware = std::thread::hardware_concurrency();
        threadCount = hardware > 1 ? hardware - 1 : 1;
    }

    try {
        workers_.reserve(threadCount);
        for (unsigned i = 0; i < threadCount; ++i) {
#ifdef SOKOBAN_ENABLE_TEST_HOOKS
            maybeFailWorkerCreationForTesting();
#endif
            workers_.emplace_back([this] { workerLoop(); });
        }
    } catch (...) {
#ifdef SOKOBAN_ENABLE_TEST_HOOKS
        workerCreationsBeforeFailureForTesting.store(
            -1, std::memory_order_relaxed);
#endif
        // The destructor will not run when construction fails. Stop and join
        // every worker already placed in the vector before its std::thread
        // destructor can observe a joinable thread and terminate the process.
        stopAndJoinWorkers();
        throw;
    }
}

TaskSystem::~TaskSystem()
{
    stopAndJoinWorkers();
}

void TaskSystem::stopAndJoinWorkers() noexcept
{
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void TaskSystem::push(std::function<void()> task)
{
    {
        const std::scoped_lock lock(mutex_);
        queue_.push_back(std::move(task));
    }
    condition_.notify_one();
}

void TaskSystem::workerLoop()
{
#ifdef SOKOBAN_ENABLE_TEST_HOOKS
    const LiveWorkerRegistration liveWorker;
#endif
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) {
                return; // stopping, queue drained
            }
            task = std::move(queue_.front());
            queue_.pop_front();
        }
        task();
        executedTasks_.fetch_add(1, std::memory_order_relaxed);
    }
}

#ifdef SOKOBAN_ENABLE_TEST_HOOKS
void TaskSystem::failWorkerCreationAfterForTesting(
    unsigned successfulWorkerCreations)
{
    workerCreationsBeforeFailureForTesting.store(
        successfulWorkerCreations, std::memory_order_relaxed);
}

unsigned TaskSystem::liveWorkerCountForTesting()
{
    return liveWorkersForTesting.load(std::memory_order_relaxed);
}
#endif

void TaskSystem::parallelFor(size_t count, size_t minChunk, const std::function<void(size_t, size_t)>& fn)
{
    minChunk = std::max<size_t>(minChunk, 1);
    if (count == 0) {
        return;
    }
    if (count <= minChunk || workers_.empty()) {
        fn(0, count);
        return;
    }

    // Aim for a few chunks per thread so uneven chunk costs still balance,
    // while never dropping below minChunk.
    const size_t threads = workers_.size() + 1; // workers + calling thread
    const size_t chunkSize = std::max(minChunk, (count + threads * 4 - 1) / (threads * 4));

    const size_t helperCount = std::min(workers_.size(), (count + chunkSize - 1) / chunkSize);
    const auto state = std::make_shared<ParallelForState>(
        count, chunkSize, helperCount, fn);

    size_t queuedHelpers = 0;
    try {
        for (; queuedHelpers < helperCount; ++queuedHelpers) {
            push([state] {
                state->runChunks();
                state->helperComplete();
            });
        }
    } catch (...) {
        state->recordFailure(std::current_exception());
        state->helpersNotQueued(helperCount - queuedHelpers);
    }

    state->runChunks(); // the calling thread participates
    state->waitForHelpers();
    state->rethrowFailure();
}

TaskSystem& taskSystem()
{
    static TaskSystem system;
    return system;
}

} // namespace sokoban
