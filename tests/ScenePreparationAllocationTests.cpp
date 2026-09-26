#include "TestHarness.hpp"

#include "engine/TaskSystem.hpp"
#include "engine/render/IsoScenePreparer.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

namespace allocationTracking {

struct Header {
    void* base = nullptr;
};

std::atomic_bool measuring = false;
std::atomic_uint64_t allocationCount = 0;
std::atomic_uint64_t allocatedBytes = 0;

void* allocate(std::size_t size, std::size_t alignment)
{
    const std::size_t storedSize = std::max(size, std::size_t { 1 });
    if (storedSize > std::numeric_limits<std::size_t>::max() -
            sizeof(Header) - alignment) {
        throw std::bad_alloc();
    }
    void* const base = std::malloc(
        storedSize + sizeof(Header) + alignment - 1);
    if (!base) {
        throw std::bad_alloc();
    }
    const std::uintptr_t first = reinterpret_cast<std::uintptr_t>(base) +
        sizeof(Header);
    const std::uintptr_t aligned =
        (first + alignment - 1) & ~(alignment - 1);
    auto* const header = reinterpret_cast<Header*>(aligned) - 1;
    header->base = base;
    if (measuring.load(std::memory_order_relaxed)) {
        allocationCount.fetch_add(1, std::memory_order_relaxed);
        allocatedBytes.fetch_add(size, std::memory_order_relaxed);
    }
    return reinterpret_cast<void*>(aligned);
}

void deallocate(void* pointer) noexcept
{
    if (!pointer) {
        return;
    }
    const auto* const header =
        reinterpret_cast<const Header*>(pointer) - 1;
    std::free(header->base);
}

struct Sample {
    uint64_t allocations = 0;
    uint64_t bytes = 0;
};

template <typename Function>
Sample measure(Function&& function)
{
    allocationCount.store(0, std::memory_order_relaxed);
    allocatedBytes.store(0, std::memory_order_relaxed);
    measuring.store(true, std::memory_order_release);
    try {
        function();
    } catch (...) {
        measuring.store(false, std::memory_order_release);
        throw;
    }
    measuring.store(false, std::memory_order_release);
    return {
        .allocations = allocationCount.load(std::memory_order_relaxed),
        .bytes = allocatedBytes.load(std::memory_order_relaxed),
    };
}

} // namespace allocationTracking

void* operator new(std::size_t size)
{
    return allocationTracking::allocate(size, alignof(std::max_align_t));
}

void* operator new[](std::size_t size)
{
    return allocationTracking::allocate(size, alignof(std::max_align_t));
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return allocationTracking::allocate(
        size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return allocationTracking::allocate(
        size, static_cast<std::size_t>(alignment));
}

void operator delete(void* pointer) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete[](void* pointer) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete(void* pointer, std::align_val_t) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete[](void* pointer, std::align_val_t) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete[](
    void* pointer,
    std::size_t,
    std::align_val_t) noexcept
{
    allocationTracking::deallocate(pointer);
}

namespace {

sokoban::RenderFrameData makeScene(uint32_t edge)
{
    sokoban::RenderFrameData frame;
    frame.viewMode = sokoban::RenderViewMode::Isometric3D;
    frame.levelWidth = edge;
    frame.levelHeight = edge;
    frame.lighting.shadows.enabled = true;
    frame.lighting.pointLightCount = 4;
    frame.tiles.reserve(static_cast<std::size_t>(edge) * edge);
    for (uint32_t y = 0; y < edge; ++y) {
        for (uint32_t x = 0; x < edge; ++x) {
            frame.tiles.push_back({
                .cell = {
                    static_cast<int>(x), static_cast<int>(y), 0 },
                .position = {
                    static_cast<float>(x), static_cast<float>(y) },
                .color = { 0.4f, 0.5f, 0.3f, 1.0f },
                .height = 1.0f,
                .renderableId = static_cast<uint64_t>(y) * edge + x + 1,
            });
        }
    }
    return frame;
}

void testWarmPreparationAllocationCounts()
{
    constexpr std::size_t measuredFrames = 64;
    const sokoban::RenderFrameData frame = makeScene(32);
    sokoban::TaskSystem tasks(2);

    sokoban::IsoScenePreparer serialPreparer;
    sokoban::PreparedRenderScene serialScene;
    sokoban::IsoScenePreparer parallelPreparer;
    sokoban::PreparedRenderScene parallelScene;
    for (int warmup = 0; warmup < 8; ++warmup) {
        serialPreparer.prepare(
            frame, { 1920.0f, 1080.0f }, serialScene);
        parallelPreparer.prepare(
            frame, { 1920.0f, 1080.0f }, parallelScene, &tasks);
    }

    const allocationTracking::Sample serial =
        allocationTracking::measure([&] {
            for (std::size_t index = 0; index < measuredFrames; ++index) {
                serialPreparer.prepare(
                    frame, { 1920.0f, 1080.0f }, serialScene);
            }
        });
    const allocationTracking::Sample parallel =
        allocationTracking::measure([&] {
            for (std::size_t index = 0; index < measuredFrames; ++index) {
                parallelPreparer.prepare(
                    frame, { 1920.0f, 1080.0f }, parallelScene, &tasks);
            }
        });

    std::cout << "scene_preparation_allocations frames=" << measuredFrames
              << " serial_count=" << serial.allocations
              << " serial_bytes=" << serial.bytes
              << " parallel_count=" << parallel.allocations
              << " parallel_bytes=" << parallel.bytes << '\n';
    CHECK_MESSAGE(serial.allocations == 0,
        "warm serial scene preparation stays allocation free");
    CHECK_MESSAGE(parallel.allocations == 0,
        "warm parallel scene preparation stays allocation free");
}

void testWarmParallelForAllocationCounts()
{
    constexpr std::size_t measuredLoops = 64;
    constexpr std::size_t itemCount = 4096;
    sokoban::TaskSystem tasks(2);
    std::atomic_uint64_t checksum { 0 };
    const auto work = [&](std::size_t begin, std::size_t end) {
        uint64_t local = 0;
        for (std::size_t index = begin; index < end; ++index) {
            local += index * 2654435761ULL;
        }
        checksum.fetch_add(local, std::memory_order_relaxed);
    };
    for (int warmup = 0; warmup < 8; ++warmup) {
        tasks.parallelFor(itemCount, 128, work);
    }

    const allocationTracking::Sample parallelFor =
        allocationTracking::measure([&] {
            for (std::size_t index = 0; index < measuredLoops; ++index) {
                tasks.parallelFor(itemCount, 128, work);
            }
        });

    std::cout << "parallel_for_allocations loops=" << measuredLoops
              << " count=" << parallelFor.allocations
              << " bytes=" << parallelFor.bytes
              << " checksum="
              << checksum.load(std::memory_order_relaxed) << '\n';
    CHECK_MESSAGE(parallelFor.allocations == 0,
        "warm parallelFor coordination stays allocation free");
}

} // namespace

int main()
{
    testWarmPreparationAllocationCounts();
    testWarmParallelForAllocationCounts();
    if (failures == 0) {
        std::cout << "ScenePreparationAllocationTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    return 1;
}
