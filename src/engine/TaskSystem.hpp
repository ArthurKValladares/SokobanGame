#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace sokoban {

// A small worker-thread pool for task-based parallelism. Depends only on the
// standard library, so it is usable from any engine module (including the
// headless ones) and in tests.
//
// Two usage shapes:
//   - enqueue(fn): schedules fn on a worker and returns a std::future for its
//     result. Exceptions thrown by fn surface on future.get().
//   - parallelFor(count, minChunk, fn): runs fn(begin, end) over contiguous
//     chunks of [0, count) across the workers; the calling thread
//     participates, and the call returns once every index is processed.
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
    // at or below it run inline on the calling thread.
    void parallelFor(size_t count, size_t minChunk, const std::function<void(size_t, size_t)>& fn);

    [[nodiscard]] unsigned workerCount() const { return static_cast<unsigned>(workers_.size()); }
    [[nodiscard]] uint64_t executedTaskCount() const { return executedTasks_.load(std::memory_order_relaxed); }

private:
    void push(std::function<void()> task);
    void workerLoop();

    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> queue_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<uint64_t> executedTasks_ { 0 };
    bool stopping_ = false;
};

// The engine-wide task system, created on first use.
[[nodiscard]] TaskSystem& taskSystem();

} // namespace sokoban
