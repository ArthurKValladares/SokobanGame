#include "engine/TaskSystem.hpp"

#include <algorithm>
#include <latch>

namespace sokoban {

TaskSystem::TaskSystem(unsigned threadCount)
{
    if (threadCount == 0) {
        const unsigned hardware = std::thread::hardware_concurrency();
        threadCount = hardware > 1 ? hardware - 1 : 1;
    }

    workers_.reserve(threadCount);
    for (unsigned i = 0; i < threadCount; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

TaskSystem::~TaskSystem()
{
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();
    for (std::thread& worker : workers_) {
        worker.join();
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

    std::atomic<size_t> nextIndex { 0 };
    auto runChunks = [&] {
        while (true) {
            const size_t begin = nextIndex.fetch_add(chunkSize, std::memory_order_relaxed);
            if (begin >= count) {
                return;
            }
            fn(begin, std::min(begin + chunkSize, count));
        }
    };

    const size_t helperCount = std::min(workers_.size(), (count + chunkSize - 1) / chunkSize);
    std::latch helpersDone(static_cast<ptrdiff_t>(helperCount));
    for (size_t i = 0; i < helperCount; ++i) {
        push([&runChunks, &helpersDone] {
            runChunks();
            helpersDone.count_down();
        });
    }

    runChunks(); // the calling thread participates
    helpersDone.wait();
}

TaskSystem& taskSystem()
{
    static TaskSystem system;
    return system;
}

} // namespace sokoban
