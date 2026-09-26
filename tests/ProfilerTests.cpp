#include "TestHarness.hpp"

#include "engine/Profiler.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>

namespace {

void waitBriefly()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void testNestedScopesAndHotPaths()
{
    sokoban::CpuProfiler& profiler = sokoban::CpuProfiler::instance();
    profiler.setEnabled(true);
    profiler.setPaused(false);
    profiler.clearCapture();
    profiler.setCurrentThreadName("Profiler test");
    profiler.beginFrame(42);
    {
        sokoban::CpuProfileScope outer("Outer");
        waitBriefly();
        {
            sokoban::CpuProfileScope inner("Inner");
            waitBriefly();
        }
    }
    profiler.endFrame();

    const sokoban::CpuProfileFrame frame = profiler.latestFrame();
    CHECK_MESSAGE(frame.frameIndex == 42, "retains the frame identity");
    CHECK_MESSAGE(frame.events.size() == 2, "captures nested events");
    CHECK_MESSAGE(frame.hotPaths.size() == 2, "aggregates both scope names");
    const auto outer = std::ranges::find_if(
        frame.events,
        [](const sokoban::CpuProfileEvent& event) {
            return event.name == "Outer";
        });
    const auto inner = std::ranges::find_if(
        frame.events,
        [](const sokoban::CpuProfileEvent& event) {
            return event.name == "Inner";
        });
    CHECK_MESSAGE(outer != frame.events.end(), "outer event is present");
    CHECK_MESSAGE(inner != frame.events.end(), "inner event is present");
    if (outer != frame.events.end() && inner != frame.events.end()) {
        CHECK_MESSAGE(outer->depth == 0, "outer scope has depth zero");
        CHECK_MESSAGE(inner->depth == 1, "inner scope has nested depth");
        CHECK_MESSAGE(
            outer->durationMilliseconds >= inner->durationMilliseconds,
            "outer duration includes inner duration");
    }
}

void testPauseAndBoundedHistory()
{
    sokoban::CpuProfiler& profiler = sokoban::CpuProfiler::instance();
    profiler.clearCapture();
    profiler.setPaused(true);
    profiler.beginFrame(1);
    profiler.endFrame();
    CHECK_MESSAGE(
        profiler.capturedFrames().empty(),
        "paused profiler does not append frames");
    profiler.setPaused(false);
    for (std::size_t index = 0;
         index < sokoban::CpuProfiler::capturedFrameCapacity + 3;
         ++index) {
        profiler.beginFrame(index + 1);
        profiler.endFrame();
    }
    const auto frames = profiler.capturedFrames();
    CHECK_MESSAGE(
        frames.size() == sokoban::CpuProfiler::capturedFrameCapacity,
        "capture history remains bounded");
    CHECK_MESSAGE(frames.front().frameIndex == 4, "history drops oldest frames");
}

void testWorkerAndCrossFrameScopes()
{
    sokoban::CpuProfiler& profiler = sokoban::CpuProfiler::instance();
    profiler.clearCapture();
    profiler.beginFrame(500);
    std::thread worker([&] {
        profiler.setCurrentThreadName("Test worker");
        sokoban::CpuProfileScope scope("Worker scope");
        waitBriefly();
    });
    worker.join();
    auto crossing = std::make_unique<sokoban::CpuProfileScope>("Cross-frame");
    profiler.endFrame();

    const sokoban::CpuProfileFrame first = profiler.latestFrame();
    CHECK_MESSAGE(
        std::ranges::any_of(first.events, [](const auto& event) {
            return event.name == "Worker scope";
        }),
        "collects worker-thread scopes");
    CHECK_MESSAGE(
        std::ranges::any_of(first.threads, [](const auto& thread) {
            return thread.name == "Test worker";
        }),
        "retains worker-thread names");

    profiler.beginFrame(501);
    crossing.reset();
    profiler.endFrame();
    const sokoban::CpuProfileFrame second = profiler.latestFrame();
    CHECK_MESSAGE(
        std::ranges::any_of(second.events, [](const auto& event) {
            return event.name == "Cross-frame";
        }),
        "publishes a cross-frame scope when it completes");
}

} // namespace

int main()
{
    testNestedScopesAndHotPaths();
    testPauseAndBoundedHistory();
    testWorkerAndCrossFrameScopes();
    if (failures == 0) {
        std::cout << "ProfilerTests: passed\n";
        return 0;
    }
    return 1;
}
