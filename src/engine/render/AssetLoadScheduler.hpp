#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace sokoban {

// CPU preparation is deliberately scheduled separately from Vulkan
// publication. This keeps content streaming responsive when a level changes:
// visible content wins, while speculative work can be discarded before it
// touches disk or allocates decoded data.
enum class AssetLoadKind : uint8_t {
    Model,
    Animation,
    Texture,
};

struct AssetLoadKey {
    AssetLoadKind kind = AssetLoadKind::Model;
    uint32_t index = 0;

    [[nodiscard]] bool operator==(const AssetLoadKey&) const = default;
};

enum class AssetLoadPriority : uint8_t {
    // Required by the frame being presented now.
    Visible,
    // Needed by the loaded world soon but not drawn yet: first-use clips and
    // effects, and the bodies a board can switch to. Starts after visible
    // work and survives prefetch cancellation.
    Upcoming,
    // A likely next screen or world; safe to abandon on a transition.
    Prefetch,
};

struct AssetLoadingBudget {
    // Parsing and image decode can be memory- and IO-heavy. Keep room for
    // simulation, audio, and the OS rather than filling every worker thread.
    // The application raises this from the worker count; see
    // assetCpuJobsForWorkers().
    std::size_t maxConcurrentCpuJobs = 2;
    // Vulkan publication runs on the render thread, so it has its own frame
    // budget independent of background preparation. A frame stops starting
    // publications at whichever limit it reaches first: this count, or the
    // time budget below for the kind of work it is doing. The first
    // publication of a frame is always allowed, so a slow one cannot stall
    // loading.
    std::size_t maxPublicationsPerFrame = 64;
    // While the presented frame is still missing assets it asked for, the
    // world is visibly incomplete and finishing it is worth more than a
    // perfectly even frame time.
    uint32_t visiblePublicationMicroseconds = 4000;
    // Upcoming and prefetch work is invisible, so it stays small enough not
    // to be felt during play. Zero removes either time limit.
    uint32_t backgroundPublicationMicroseconds = 2000;
    // Logical decoded CPU payload plus reservations held by active decodes.
    // Upload staging has its own 64 MiB ring and remains separately visible
    // in transient telemetry.
    uint64_t preparedAssetBytes = 32ULL * 1024ULL * 1024ULL;
    // Residency budgets are enforced by VulkanModelResources before new GPU
    // resources are published. They exclude swapchain/frame resources.
    uint64_t modelResidencyBytes = 128ULL * 1024ULL * 1024ULL;
    uint64_t textureResidencyBytes = 256ULL * 1024ULL * 1024ULL;
};

// Background preparation shares the engine task system with the per-frame
// parallel loops, whose helpers queue behind whatever is already running. Two
// workers stay free for those loops; machines with too few workers to spare
// any keep the two jobs the scheduler has always allowed.
[[nodiscard]] constexpr std::size_t assetCpuJobsForWorkers(
    std::size_t workerCount) noexcept
{
    constexpr std::size_t reservedWorkers = 2;
    constexpr std::size_t minimumJobs = 2;
    constexpr std::size_t maximumJobs = 8;
    const std::size_t spare =
        workerCount > reservedWorkers ? workerCount - reservedWorkers : 0;
    return spare < minimumJobs
        ? minimumJobs
        : (spare > maximumJobs ? maximumJobs : spare);
}

class AssetLoadScheduler {
public:
    explicit AssetLoadScheduler(AssetLoadingBudget budget = {});

    // Adds a job or raises its priority. Re-requesting an active job is a
    // no-op: filesystem work already started cannot safely be interrupted.
    void request(
        AssetLoadKey key,
        AssetLoadPriority priority,
        uint64_t estimatedPreparedBytes);
    // Returns the highest-priority queued job when the CPU budget permits and
    // marks it active. The caller must call complete() once its future exists.
    [[nodiscard]] std::optional<AssetLoadKey> beginNext(
        uint64_t retainedPreparedBytes);
    void complete(AssetLoadKey key);

    // Removes prefetch jobs that have not started. Visible and upcoming work
    // is kept. The returned keys let the owner reset its matching resource
    // slots to Unrequested.
    [[nodiscard]] std::vector<AssetLoadKey> cancelQueuedPrefetches();
    // Also removes upcoming jobs that have not started, keeping only visible
    // work. For a world change: what the previous world was about to need is
    // no longer worth loading ahead of what the new one needs.
    [[nodiscard]] std::vector<AssetLoadKey> cancelQueuedBackgroundWork();
    void clear();

    [[nodiscard]] std::size_t queuedCount() const;
    [[nodiscard]] std::size_t activeCount() const { return activeCount_; }
    [[nodiscard]] uint64_t activePreparedBytes() const
    {
        return activePreparedBytes_;
    }
    [[nodiscard]] uint64_t preparedBudgetDeferrals() const
    {
        return preparedBudgetDeferrals_;
    }
    [[nodiscard]] uint64_t oversizedAssetStarts() const
    {
        return oversizedAssetStarts_;
    }
    [[nodiscard]] uint64_t cancelledPrefetchCount() const
    {
        return cancelledPrefetchCount_;
    }
    [[nodiscard]] const AssetLoadingBudget& budget() const { return budget_; }

private:
    struct Entry {
        AssetLoadPriority priority = AssetLoadPriority::Prefetch;
        uint64_t sequence = 0;
        uint64_t estimatedPreparedBytes = 0;
        bool active = false;
    };

    struct KeyHash {
        [[nodiscard]] std::size_t operator()(AssetLoadKey key) const
        {
            return (static_cast<std::size_t>(key.kind) << 32) ^ key.index;
        }
    };

    // Removes every queued job weaker than `weakestKept`.
    [[nodiscard]] std::vector<AssetLoadKey> cancelQueuedWeakerThan(
        AssetLoadPriority weakestKept);

    AssetLoadingBudget budget_;
    std::unordered_map<AssetLoadKey, Entry, KeyHash> entries_;
    uint64_t nextSequence_ = 1;
    uint64_t cancelledPrefetchCount_ = 0;
    uint64_t activePreparedBytes_ = 0;
    uint64_t preparedBudgetDeferrals_ = 0;
    uint64_t oversizedAssetStarts_ = 0;
    std::size_t activeCount_ = 0;
};

} // namespace sokoban
