#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <latch>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace sokoban {

// A small worker-thread pool for task-based parallelism. Depends only on the
// standard library, so it is usable from any engine module (including the
// headless ones) and in tests.
//
// Two usage shapes:
//   - enqueue(fn): schedules fn on a worker and returns a std::future for its
//     result. Exceptions thrown by fn surface on future.get().
//   - scopedTask(fn): schedules void work whose lifetime cannot escape the
//     returned stack object. finish() propagates failures; destruction waits.
//   - parallelFor(count, minChunk, fn): runs fn(begin, end) over contiguous
//     chunks of [0, count) across the workers; the calling thread
//     participates. If a chunk throws, no new chunks are started, in-flight
//     chunks finish, and one captured exception is rethrown on the caller.
//
// Tasks must not block waiting on other tasks (there is no dependency
// tracking or work stealing yet); keep them independent. That constraint is
// what future task-graph work would relax.
class TaskSystem {
public:
    // threadCount 0 picks a count based on the hardware (leaving a core for
    // the calling thread).
    explicit TaskSystem(unsigned threadCount = 0);
    ~TaskSystem();

    TaskSystem(const TaskSystem&) = delete;
    TaskSystem& operator=(const TaskSystem&) = delete;

    // Stack-owned one-shot work for a latency-sensitive caller that will do
    // useful work in parallel and then join before leaving its scope. Unlike
    // enqueue(), this needs no packaged_task/future shared state: both the
    // callable and completion latch live in the caller's ScopedTask. The
    // destructor always waits, so references captured by the callable remain
    // valid when foreground work unwinds with an exception.
    template <typename Function>
    class ScopedTask {
    public:
        ScopedTask(TaskSystem& system, Function function)
            : function_(std::move(function))
        {
            static_assert(std::is_void_v<std::invoke_result_t<Function&>>,
                "TaskSystem::ScopedTask requires a void callable");
            system.push([this] { run(); });
        }

        ~ScopedTask()
        {
            wait();
        }

        ScopedTask(const ScopedTask&) = delete;
        ScopedTask& operator=(const ScopedTask&) = delete;
        ScopedTask(ScopedTask&&) = delete;
        ScopedTask& operator=(ScopedTask&&) = delete;

        void finish()
        {
            wait();
            if (failure_) {
                std::rethrow_exception(failure_);
            }
        }

    private:
        void run() noexcept
        {
            try {
                function_();
            } catch (...) {
                failure_ = std::current_exception();
            }
            completed_.count_down();
        }

        void wait() noexcept
        {
            if (!finished_) {
                completed_.wait();
                finished_ = true;
            }
        }

        Function function_;
        std::latch completed_ { 1 };
        std::exception_ptr failure_;
        bool finished_ = false;
    };

    template <typename Function>
    [[nodiscard]] auto scopedTask(Function function)
    {
        return ScopedTask<std::decay_t<Function>>(
            *this, std::move(function));
    }

    template <typename Fn>
    [[nodiscard]] auto enqueue(Fn fn) -> std::future<std::invoke_result_t<Fn>>
    {
        using Result = std::invoke_result_t<Fn>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::move(fn));
        std::future<Result> future = task->get_future();
        push([task] { (*task)(); });
        return future;
    }

    // Chunked parallel loop. fn is invoked as fn(begin, end) with disjoint
    // ranges covering [0, count). minChunk bounds scheduling overhead: counts
    // at or below it run inline on the calling thread. The call does not return
    // or rethrow until every helper scheduled by this invocation has finished.
    void parallelFor(size_t count, size_t minChunk, const std::function<void(size_t, size_t)>& fn);

    [[nodiscard]] unsigned workerCount() const { return static_cast<unsigned>(workers_.size()); }
    [[nodiscard]] uint64_t executedTaskCount() const { return executedTasks_.load(std::memory_order_relaxed); }

#ifdef SOKOBAN_ENABLE_TEST_HOOKS
    // Makes one later construction fail after this many workers have started.
    // The hook resets when it fires so subsequent constructions are ordinary.
    static void failWorkerCreationAfterForTesting(
        unsigned successfulWorkerCreations);
    [[nodiscard]] static unsigned liveWorkerCountForTesting();
#endif

private:
    void push(std::function<void()> task);
    void workerLoop();
    void stopAndJoinWorkers() noexcept;

    std::vector<std::thread> workers_;
    // Retained FIFO ring. std::deque is allowed to release an emptied block
    // and allocate another as its ends advance, which turned a one-task-per-
    // frame producer into periodic allocator traffic on some standard library
    // implementations. This grows under a real queue-depth increase and then
    // reuses every slot indefinitely.
    std::vector<std::function<void()>> queue_;
    std::size_t queueHead_ = 0;
    std::size_t queuedTaskCount_ = 0;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<uint64_t> executedTasks_ { 0 };
    bool stopping_ = false;
};

// The engine-wide task system, created on first use.
[[nodiscard]] TaskSystem& taskSystem();

} // namespace sokoban
