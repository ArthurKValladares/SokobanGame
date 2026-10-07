#include "engine/render/GroundRimSurfaceCache.hpp"
#include "engine/render/ProcessedGroundArtifact.hpp"

#include <algorithm>
#include <limits>

namespace sokoban {

std::vector<CompiledGroundRimSurface> compileGroundRimSurfaces(
    std::span<const RenderFrameData::Tile> tiles)
{
    std::vector<CompiledGroundRimSurface> result;
    result.reserve(static_cast<std::size_t>(std::ranges::count_if(
        tiles, hasGroundRimSurface)));
    for (std::size_t tileIndex = 0; tileIndex < tiles.size(); ++tileIndex) {
        if (hasGroundRimSurface(tiles[tileIndex])) {
            result.push_back({ tileIndex, buildGroundRimSurface(tiles[tileIndex]) });
        }
    }
    return result;
}

void GroundRimSurfaceCache::rebuildMappings()
{
    mappings_.clear();
    mappings_.reserve(candidates_.size());
    for (std::size_t index = 0; index < candidates_.size(); ++index) {
        mappings_.push_back({ candidates_[index].tileIndex, entries_[index].surfaceSlot });
    }
    std::ranges::sort(mappings_, {}, &Mapping::tileIndex);
}

void GroundRimSurfaceCache::update(std::span<const RenderFrameData::Tile> tiles,
    const ProcessedGroundArtifact* artifact)
{
    try {
        importedSurfaceCount_ = 0;
        candidates_.clear();
        for (std::size_t tileIndex = 0; tileIndex < tiles.size(); ++tileIndex) {
            if (hasGroundRimSurface(tiles[tileIndex])) {
                candidates_.push_back({ groundRimSurfaceKey(tiles[tileIndex]), tileIndex });
            }
        }
        std::ranges::sort(candidates_, [](const Candidate& left, const Candidate& right) {
            if (left.key != right.key) return left.key < right.key;
            return left.tileIndex < right.tileIndex;
        });

        const bool reused = valid_ && candidates_.size() == entries_.size() &&
            std::ranges::equal(candidates_, entries_, {}, &Candidate::key, &Entry::key);
        if (reused) {
            rebuildMappings();
            lastUpdateReused_ = true;
            reusedSurfaceCount_ = entries_.size();
            generatedSurfaceCount_ = 0;
            ++hitCount_;
            return;
        }

        const bool canReuseEntries = valid_;
        valid_ = false;
        lastUpdateReused_ = false;
        reusedSurfaceCount_ = 0;
        generatedSurfaceCount_ = 0;
        workingEntries_.clear();
        // Warm both lightweight entry buffers once, so the first later edit
        // can swap them without allocating. Surface storage is retained only
        // once, rather than doubling the large patch arrays for rebuilding.
        entries_.reserve(candidates_.size());
        workingEntries_.reserve(candidates_.size());
        surfaces_.reserve(std::max(surfaces_.size(), candidates_.size()));
        freeSlots_.reserve(std::max(surfaces_.size(), candidates_.size()));

        constexpr std::size_t unassigned = std::numeric_limits<std::size_t>::max();
        std::size_t previousIndex = 0;
        for (const Candidate& candidate : candidates_) {
            if (canReuseEntries) {
                while (previousIndex < entries_.size() && entries_[previousIndex].key < candidate.key) {
                    freeSlots_.push_back(entries_[previousIndex++].surfaceSlot);
                }
                if (previousIndex < entries_.size() && entries_[previousIndex].key == candidate.key) {
                    workingEntries_.push_back(entries_[previousIndex++]);
                    ++reusedSurfaceCount_;
                    continue;
                }
            }
            workingEntries_.push_back({ candidate.key, unassigned });
        }
        if (canReuseEntries) {
            while (previousIndex < entries_.size()) {
                freeSlots_.push_back(entries_[previousIndex++].surfaceSlot);
            }
        }

        for (std::size_t index = 0; index < workingEntries_.size(); ++index) {
            Entry& entry = workingEntries_[index];
            if (entry.surfaceSlot != unassigned) continue;
            if (freeSlots_.empty()) {
                entry.surfaceSlot = surfaces_.size();
                surfaces_.emplace_back();
            } else {
                entry.surfaceSlot = freeSlots_.back();
                freeSlots_.pop_back();
            }
            const GroundRimSurface* baked = artifact ? artifact->find(entry.key) : nullptr;
            if (baked) {
                surfaces_[entry.surfaceSlot] = *baked;
                entry.baked = true;
                ++importedSurfaceCount_;
            } else {
                surfaces_[entry.surfaceSlot] = buildGroundRimSurface(tiles[candidates_[index].tileIndex]);
                entry.baked = false;
                ++generatedSurfaceCount_;
            }
        }
        entries_.swap(workingEntries_);
        workingEntries_.clear();
        rebuildMappings();
        valid_ = true;
        bakedSurfaceCount_ = static_cast<std::size_t>(std::ranges::count_if(
            entries_, &Entry::baked));
        bakedImportCount_ += importedSurfaceCount_;
        ++rebuildCount_;
    } catch (...) {
        // No partially remapped or overwritten surface can become a future
        // hit. Capacity remains useful when the caller retries preparation.
        invalidate();
        throw;
    }
}

const GroundRimSurface* GroundRimSurfaceCache::surfaceForTileIndex(
    std::size_t tileIndex) const noexcept
{
    if (!valid_) return nullptr;
    const auto found = std::ranges::lower_bound(mappings_, tileIndex, {}, &Mapping::tileIndex);
    if (found == mappings_.end() || found->tileIndex != tileIndex) return nullptr;
    return &surfaces_[found->surfaceSlot];
}

std::size_t GroundRimSurfaceCache::capacityBytes() const noexcept
{
    return surfaces_.capacity() * sizeof(GroundRimSurface) +
        freeSlots_.capacity() * sizeof(std::size_t) +
        (entries_.capacity() + workingEntries_.capacity()) * sizeof(Entry) +
        candidates_.capacity() * sizeof(Candidate) +
        mappings_.capacity() * sizeof(Mapping);
}

void GroundRimSurfaceCache::invalidate() noexcept
{
    valid_ = false;
    lastUpdateReused_ = false;
    reusedSurfaceCount_ = 0;
    generatedSurfaceCount_ = 0;
    importedSurfaceCount_ = 0;
    bakedSurfaceCount_ = 0;
    surfaces_.clear();
    freeSlots_.clear();
    entries_.clear();
    workingEntries_.clear();
    candidates_.clear();
    mappings_.clear();
}

} // namespace sokoban
