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
    if (budget_.preparedAssetBytes == 0) {
        throw std::invalid_argument(
            "Prepared asset memory budget must be greater than zero");
    }
}

void AssetLoadScheduler::request(
    AssetLoadKey key,
    AssetLoadPriority priority,
    uint64_t estimatedPreparedBytes)
{
    // Zero is reserved for legacy or unavailable metadata. Charging the full
    // budget makes unknown work run alone instead of bypassing admission.
    estimatedPreparedBytes = estimatedPreparedBytes == 0
        ? budget_.preparedAssetBytes
        : estimatedPreparedBytes;
    const auto [iterator, inserted] = entries_.try_emplace(
        key,
        Entry {
            .priority = priority,
            .sequence = nextSequence_++,
            .estimatedPreparedBytes = estimatedPreparedBytes,
        });
    if (!inserted && !iterator->second.active) {
        iterator->second.estimatedPreparedBytes = estimatedPreparedBytes;
        if (priority < iterator->second.priority) {
            iterator->second.priority = priority;
        }
    }
}

std::optional<AssetLoadKey> AssetLoadScheduler::beginNext(
    uint64_t retainedPreparedBytes)
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

    const uint64_t estimate = selected->second.estimatedPreparedBytes;
    const bool oversized = estimate > budget_.preparedAssetBytes;
    bool admitted = false;
    if (oversized) {
        admitted = retainedPreparedBytes == 0 && activePreparedBytes_ == 0;
    } else if (retainedPreparedBytes <= budget_.preparedAssetBytes &&
        activePreparedBytes_ <=
            budget_.preparedAssetBytes - retainedPreparedBytes) {
        const uint64_t available = budget_.preparedAssetBytes -
            retainedPreparedBytes - activePreparedBytes_;
        admitted = estimate <= available;
    }
    if (!admitted) {
        ++preparedBudgetDeferrals_;
        return std::nullopt;
    }

    selected->second.active = true;
    ++activeCount_;
    activePreparedBytes_ += estimate;
    if (oversized) {
        ++oversizedAssetStarts_;
    }
    return selected->first;
}

void AssetLoadScheduler::complete(AssetLoadKey key)
{
    const auto iterator = entries_.find(key);
    if (iterator == entries_.end() || !iterator->second.active) {
        throw std::logic_error("Completed an asset-loading job that was not active");
    }
    activePreparedBytes_ -= iterator->second.estimatedPreparedBytes;
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
    activePreparedBytes_ = 0;
    cancelledPrefetchCount_ = 0;
    preparedBudgetDeferrals_ = 0;
    oversizedAssetStarts_ = 0;
    nextSequence_ = 1;
}

std::size_t AssetLoadScheduler::queuedCount() const
{
    return entries_.size() - activeCount_;
}

} // namespace sokoban
