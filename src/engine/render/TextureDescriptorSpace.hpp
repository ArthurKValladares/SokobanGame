#pragma once

#include <cstdint>
#include <vector>

namespace sokoban {

// How the texture descriptor heap is divided, and where a logical texture index
// lands in it.
//
// The heap is one fixed-capacity array and two populations share it. Manifest
// textures take the low indices and keep them, because an editor writes a
// RenderTexture id into a level file and slot 7 has to still be slot 7 next
// session. glTF textures discovered while reading models take the top of the
// range downward, since their count is not known until the models are loaded.
// The gap left in the middle is what lets the manifest grow later.
//
// The one thing that must never happen is the two meeting. That test lived in
// three places in three wordings, and the index formula itself lived in two.
// They are here now.
//
// Deliberately not owning the throw. The three callers raise different
// exception types with different messages - `std::out_of_range` from the
// catalog, `std::runtime_error` from the renderer - and those are part of their
// contracts. What is shared is the arithmetic and the question; what to do
// about a bad answer stays with the caller.
class TextureDescriptorSpace {
public:
    // Where the discovered range starts, given how many textures were found.
    //
    // Underflows if `discoveredCount` exceeds `capacity`, which the callers
    // rule out before asking - the catalog by checking its texture count
    // against the capacity, the renderer by construction.
    [[nodiscard]] static constexpr uint32_t discoveredBaseFor(
        uint32_t capacity, uint32_t discoveredCount)
    {
        return capacity - discoveredCount;
    }

    // Whether the two populations collide. Equal is not a collision: the
    // manifest range is half-open, so a manifest count exactly equal to the
    // discovered base means they meet without overlapping.
    [[nodiscard]] static constexpr bool rangesOverlap(
        uint32_t manifestCount, uint32_t discoveredBase)
    {
        return manifestCount > discoveredBase;
    }

    // A logical texture index as the descriptor heap sees it. Manifest indices
    // pass through unchanged; discovered ones are rebased onto the high range
    // in the order they were found.
    [[nodiscard]] static constexpr uint32_t descriptorIndexFor(
        uint32_t logicalIndex, uint32_t manifestCount, uint32_t discoveredBase)
    {
        return logicalIndex < manifestCount
            ? logicalIndex
            : discoveredBase + logicalIndex - manifestCount;
    }

    // --- the renderer's live partition ------------------------------------

    // Lay out the heap. The caller checks rangesOverlap() first and decides
    // what to say about it; this does not re-check.
    void reset(uint32_t capacity, uint32_t manifestCount, uint32_t discoveredCount)
    {
        capacity_ = capacity;
        manifestCount_ = manifestCount;
        discoveredBase_ = discoveredBaseFor(capacity, discoveredCount);
        active_.clear();
    }

    void clear()
    {
        capacity_ = 0;
        manifestCount_ = 0;
        discoveredBase_ = 0;
        active_.clear();
    }

    [[nodiscard]] uint32_t capacity() const { return capacity_; }
    [[nodiscard]] uint32_t manifestCount() const { return manifestCount_; }
    [[nodiscard]] uint32_t discoveredBase() const { return discoveredBase_; }

    [[nodiscard]] uint32_t descriptorIndexFor(uint32_t logicalIndex) const
    {
        return descriptorIndexFor(logicalIndex, manifestCount_, discoveredBase_);
    }

    // Whether a descriptor index belongs to the stable manifest range. The
    // renderer asks this to decide whether a RenderTexture id from content is
    // one it may resolve.
    [[nodiscard]] bool isManifestTexture(uint32_t descriptorIndex) const
    {
        return descriptorIndex < manifestCount_;
    }

    // Room the manifest still has before it would run into the discovered
    // range. Saturates rather than wrapping, so an already-overlapping space
    // reports none instead of a very large number.
    [[nodiscard]] uint32_t manifestHeadroom() const
    {
        return discoveredBase_ > manifestCount_
            ? discoveredBase_ - manifestCount_
            : 0;
    }

    // Whether the manifest could grow to `count` entries. The caller throws.
    [[nodiscard]] bool manifestCanHold(uint32_t count) const
    {
        return count <= discoveredBase_;
    }

    // Take the manifest range up to `count`. Indices already claimed are left
    // alone, so this is the append the manifest sync performs.
    void growManifestRange(uint32_t count)
    {
        for (uint32_t index = manifestCount_; index < count; ++index) {
            active_.push_back(index);
        }
        manifestCount_ = count > manifestCount_ ? count : manifestCount_;
    }

    void markActive(uint32_t descriptorIndex)
    {
        active_.push_back(descriptorIndex);
    }

    void reserveActive(std::size_t count) { active_.reserve(count); }

    // Every slot in use, in the order it was claimed.
    [[nodiscard]] const std::vector<uint32_t>& active() const { return active_; }

private:
    uint32_t capacity_ = 0;
    uint32_t manifestCount_ = 0;
    uint32_t discoveredBase_ = 0;
    std::vector<uint32_t> active_;
};

} // namespace sokoban
