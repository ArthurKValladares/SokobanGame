#include "engine/render/AssetLoadScheduler.hpp"

#include <algorithm>
#include <stdexcept>

namespace sokoban {

AssetLoadScheduler::AssetLoadScheduler(AssetLoadingBudget budget)
    : budget_(budget)
{
    if (budget_.maxConcurrentCpuJobs == 0) {
        throw std::invalid_argument(
            "Asset loading CPU-job budget must be greater than zero");
    }
    if (budget_.maxPublicationsPerFrame == 0) {
        throw std::invalid_argument(
            "Asset loading publication budget must be greater than zero");
    }
}

void AssetLoadScheduler::request(
    AssetLoadKey key,
    AssetLoadPriority priority)
{
    const auto [iterator, inserted] = entries_.try_emplace(
        key,
        Entry { .priority = priority, .sequence = nextSequence_++ });
    if (!inserted && !iterator->second.active &&
        priority < iterator->second.priority) {
        iterator->second.priority = priority;
    }
}

std::optional<AssetLoadKey> AssetLoadScheduler::beginNext()
{
    if (activeCount_ >= budget_.maxConcurrentCpuJobs) {
        return std::nullopt;
    }

    auto selected = entries_.end();
    for (auto iterator = entries_.begin(); iterator != entries_.end(); ++iterator) {
        if (iterator->second.active) {
            continue;
        }
        if (selected == entries_.end() ||
            iterator->second.priority < selected->second.priority ||
            (iterator->second.priority == selected->second.priority &&
                iterator->second.sequence < selected->second.sequence)) {
            selected = iterator;
        }
    }
    if (selected == entries_.end()) {
        return std::nullopt;
    }

    selected->second.active = true;
    ++activeCount_;
    return selected->first;
}

void AssetLoadScheduler::complete(AssetLoadKey key)
{
    const auto iterator = entries_.find(key);
    if (iterator == entries_.end() || !iterator->second.active) {
        throw std::logic_error("Completed an asset-loading job that was not active");
    }
    entries_.erase(iterator);
    --activeCount_;
}

std::vector<AssetLoadKey> AssetLoadScheduler::cancelQueuedPrefetches()
{
    std::vector<AssetLoadKey> cancelled;
    for (auto iterator = entries_.begin(); iterator != entries_.end();) {
        if (!iterator->second.active &&
            iterator->second.priority == AssetLoadPriority::Prefetch) {
            cancelled.push_back(iterator->first);
            iterator = entries_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    cancelledPrefetchCount_ += cancelled.size();
    return cancelled;
}

void AssetLoadScheduler::clear()
{
    entries_.clear();
    activeCount_ = 0;
    cancelledPrefetchCount_ = 0;
    nextSequence_ = 1;
}

std::size_t AssetLoadScheduler::queuedCount() const
{
    return entries_.size() - activeCount_;
}

} // namespace sokoban
