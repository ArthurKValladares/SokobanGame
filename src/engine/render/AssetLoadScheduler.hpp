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
    // A likely next screen or world; safe to abandon on a transition.
    Prefetch,
};

struct AssetLoadingBudget {
    // Parsing and image decode can be memory- and IO-heavy. Keep room for
    // simulation, audio, and the OS rather than filling every worker thread.
    std::size_t maxConcurrentCpuJobs = 2;
    // Vulkan publication runs on the render thread, so it has its own small
    // frame budget independent of background preparation.
    std::size_t maxPublicationsPerFrame = 1;
    // Residency budgets are enforced by VulkanModelResources before new GPU
    // resources are published. They exclude swapchain/frame resources.
    uint64_t modelResidencyBytes = 128ULL * 1024ULL * 1024ULL;
    uint64_t textureResidencyBytes = 256ULL * 1024ULL * 1024ULL;
};

class AssetLoadScheduler {
public:
    explicit AssetLoadScheduler(AssetLoadingBudget budget = {});

    // Adds a job or raises its priority. Re-requesting an active job is a
    // no-op: filesystem work already started cannot safely be interrupted.
    void request(AssetLoadKey key, AssetLoadPriority priority);
    // Returns the highest-priority queued job when the CPU budget permits and
    // marks it active. The caller must call complete() once its future exists.
    [[nodiscard]] std::optional<AssetLoadKey> beginNext();
    void complete(AssetLoadKey key);

    // Removes prefetch jobs that have not started. The returned keys let the
    // owner reset its matching resource slots to Unrequested.
    [[nodiscard]] std::vector<AssetLoadKey> cancelQueuedPrefetches();
    void clear();

    [[nodiscard]] std::size_t queuedCount() const;
    [[nodiscard]] std::size_t activeCount() const { return activeCount_; }
    [[nodiscard]] uint64_t cancelledPrefetchCount() const
    {
        return cancelledPrefetchCount_;
    }
    [[nodiscard]] const AssetLoadingBudget& budget() const { return budget_; }

private:
    struct Entry {
        AssetLoadPriority priority = AssetLoadPriority::Prefetch;
        uint64_t sequence = 0;
        bool active = false;
    };

    struct KeyHash {
        [[nodiscard]] std::size_t operator()(AssetLoadKey key) const
        {
            return (static_cast<std::size_t>(key.kind) << 32) ^ key.index;
        }
    };

    AssetLoadingBudget budget_;
    std::unordered_map<AssetLoadKey, Entry, KeyHash> entries_;
    uint64_t nextSequence_ = 1;
    uint64_t cancelledPrefetchCount_ = 0;
    std::size_t activeCount_ = 0;
};

} // namespace sokoban
