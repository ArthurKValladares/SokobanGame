#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace sokoban {

// Byte accounting and eviction policy for one residency pool.
//
// Models and textures each keep a pool. The two were written out separately
// and had drifted only in the names they used, so the policy lives here once
// and the owner supplies the slots. Nothing here knows about Vulkan, which is
// what lets the decision ladder be tested without a device - it could not be
// reached at all while it sat in two private methods.
//
// Retiring bytes are the subtlety. An evicted resource stays charged to the
// pool until every frame fence that could still reference it has cleared, so
// residency does not drop the moment a victim is chosen. A publication that
// evicted its own victims therefore still fails, and retries once the fence
// clears, rather than oversubscribing the hard limit in the meantime.
//
// The limit is passed in rather than stored: it belongs to AssetLoadScheduler,
// and a second copy here would be a second thing to keep in step.
class ResidencyBudget {
public:
    [[nodiscard]] uint64_t resident() const { return resident_; }
    [[nodiscard]] uint64_t retiring() const { return retiring_; }
    [[nodiscard]] uint64_t peak() const { return peak_; }

    // A publication landed and its bytes are now on the device.
    void addResident(uint64_t bytes)
    {
        resident_ += bytes;
        peak_ = resident_ > peak_ ? resident_ : peak_;
    }

    // An in-place repaint swapped one resource for a differently sized one
    // without passing through retirement, which only the editor's painted
    // textures do.
    void replaceResident(uint64_t previousBytes, uint64_t bytes)
    {
        resident_ = resident_ - previousBytes + bytes;
        peak_ = resident_ > peak_ ? resident_ : peak_;
    }

    // A victim was chosen. Its bytes stay resident until its fences clear.
    void beginRetiring(uint64_t bytes) { retiring_ += bytes; }

    // Those fences cleared, so the memory is genuinely gone now.
    void finishRetiring(uint64_t bytes)
    {
        resident_ -= bytes;
        retiring_ -= bytes;
    }

    void reset()
    {
        resident_ = 0;
        retiring_ = 0;
        peak_ = 0;
    }

    [[nodiscard]] bool fits(uint64_t bytes, uint64_t limit) const
    {
        return resident_ + bytes <= limit;
    }

    // True while an earlier publication's victims are still waiting on their
    // fences. Further evictions are held off until then so that one publication
    // cannot cascade into freeing far more than the frame actually needed.
    [[nodiscard]] bool retirementPending() const { return retiring_ != 0; }

    // Whether more victims are needed: counting everything already retiring as
    // gone, does this publication still not fit?
    //
    // `retiring_` is the running total. The eviction loop's own victims are in
    // it, because retiring one raises it, and nothing else can be in it - the
    // caller refuses to start evicting at all while an earlier publication's
    // retirement is still pending. So there is one tally, not two.
    //
    // There used to be a second. The loop passed its own `scheduledBytes`
    // alongside, and both were subtracted, so every victim counted twice and
    // the loop stopped after freeing about half of what was asked for. The
    // publication was refused, the fence cleared, and the next attempt freed
    // half of the remainder - up to nine rounds, at a frame apiece, to admit
    // one asset. It never oversubscribed, which is why nothing looked wrong.
    [[nodiscard]] bool needsEviction(uint64_t bytes, uint64_t limit) const
    {
        return resident_ - retiring_ + bytes > limit;
    }

    // Room left before the limit, not counting anything already retiring.
    // Saturates at zero rather than wrapping when the pool is over its limit.
    [[nodiscard]] uint64_t headroom(uint64_t limit) const
    {
        const uint64_t held = resident_ - retiring_;
        return limit > held ? limit - held : 0;
    }

private:
    uint64_t resident_ = 0;
    uint64_t retiring_ = 0;
    uint64_t peak_ = 0;
};

// Which slots eviction may consider, in one place because three callers ask
// the same question: a slot is a candidate when it is fully resident, is not
// the asset currently being published, and was not requested for the frame
// being prepared right now. Evicting something this frame needs would only
// force it to be loaded again before the frame could be drawn.
//
// `resident` reports whether a slot holds device memory; the caller supplies
// it because the load state enum belongs to the owner, not here. `visit`
// returns false to stop early. Slots need `gpuBytes` and `lastRequested`.
template <typename Slot, typename ResidentPredicate, typename Visitor>
void forEachEvictableSlot(
    const std::vector<Slot>& slots,
    std::size_t protectedIndex,
    uint64_t visibleRequestStamp,
    ResidentPredicate resident,
    Visitor visit)
{
    for (std::size_t index = 0; index < slots.size(); ++index) {
        const Slot& slot = slots[index];
        if (index == protectedIndex || !resident(slot) || slot.gpuBytes == 0 ||
            (visibleRequestStamp != 0 &&
                slot.lastRequested == visibleRequestStamp)) {
            continue;
        }
        if (!visit(index, slot)) {
            return;
        }
    }
}

// The least recently requested candidate, or nothing when the pool holds
// nothing it is allowed to give up.
template <typename Slot, typename ResidentPredicate>
[[nodiscard]] std::optional<std::size_t> chooseResidencyVictim(
    const std::vector<Slot>& slots,
    std::size_t protectedIndex,
    uint64_t visibleRequestStamp,
    ResidentPredicate resident)
{
    std::optional<std::size_t> victim;
    forEachEvictableSlot(
        slots,
        protectedIndex,
        visibleRequestStamp,
        resident,
        [&](std::size_t index, const Slot& slot) {
            if (!victim || slot.lastRequested < slots[*victim].lastRequested) {
                victim = index;
            }
            return true;
        });
    return victim;
}

// The eviction ladder both pools run, and the counters that say why a
// publication was refused.
//
// The two pools keep separate budgets but share one retirement clock: the
// ladder drains retirements *before* it decides anything, and again after
// evicting, and the drain covers **both** pools - so a texture whose fences
// have cleared is what lets a model publish. That coupling was real and
// unnamed: it happened because a private makeResident() called a private
// method that happened to touch both queues. Here the drain is a parameter
// with a name and a contract, which is what has to be true before the model
// and texture halves can become separate classes at all.
//
// Everything the ladder needs arrives as an argument, so it runs - and is
// tested - without a device, a slot type, or a Vulkan handle.
class ResidencyLadder {
public:
    // Why a publication was refused. Kept apart because they mean different
    // things: the first two are properties of the asset against the budget,
    // the third is a property of the moment.
    enum class Block {
        AssetLargerThanBudget,
        NoMipTailFits,
        NothingEvictable,
    };

    // Admit `requiredBytes` into `budget`, evicting from `slots` if it has to.
    //
    // `resident` says whether a slot holds device memory, `retire` moves one
    // into its fence-owned queue, and `drainRetired` completes every
    // retirement whose fences have cleared - in both pools, not just this
    // one's. Returns whether the caller may publish now.
    template <typename Slot, typename Resident, typename Retire, typename Drain>
    [[nodiscard]] bool admit(
        ResidencyBudget& budget,
        std::vector<Slot>& slots,
        std::size_t protectedIndex,
        uint64_t requiredBytes,
        uint64_t limitBytes,
        uint64_t visibleRequestStamp,
        Resident resident,
        Retire retire,
        Drain drainRetired)
    {
        drainRetired();
        if (requiredBytes > limitBytes) {
            markBlocked(Block::AssetLargerThanBudget);
            return false;
        }
        if (budget.fits(requiredBytes, limitBytes)) {
            return true;
        }
        if (budget.retirementPending()) {
            // A previous publication already selected enough victims for its
            // own retry. Do not let other CPU-ready slots cascade into
            // additional evictions while the first fence-owned retirement is
            // still pending.
            blocked_ = false;
            return false;
        }

        while (budget.needsEviction(requiredBytes, limitBytes)) {
            const std::optional<std::size_t> victim = chooseResidencyVictim(
                slots, protectedIndex, visibleRequestStamp, resident);
            if (!victim) {
                markBlocked(Block::NothingEvictable);
                return false;
            }
            // retire() raises the budget's retiring total, which is what the
            // condition above reads; there is no second tally to keep in step.
            retire(slots[*victim]);
        }
        drainRetired();
        blocked_ = false;
        // Retired allocations remain part of the physical residency total
        // until every referencing frame fence completes. Keep this CPU-ready
        // asset for a later publication instead of oversubscribing the hard
        // budget.
        return budget.fits(requiredBytes, limitBytes);
    }

    // Refused for a reason the ladder itself did not decide - currently the
    // mip planner, which gives up before admit() is ever called.
    void markBlocked(Block reason)
    {
        blocked_ = true;
        ++blocks_;
        switch (reason) {
        case Block::AssetLargerThanBudget:
            ++oversizedBlocks_;
            break;
        case Block::NoMipTailFits:
            ++mipPlanBlocks_;
            break;
        case Block::NothingEvictable:
            ++noVictimBlocks_;
            break;
        }
    }

    // Counted where the victim is actually retired, which is the owner's job
    // because only it knows what a retirement costs to destroy.
    void countEviction() { ++evictions_; }

    void reset()
    {
        evictions_ = 0;
        blocks_ = 0;
        oversizedBlocks_ = 0;
        mipPlanBlocks_ = 0;
        noVictimBlocks_ = 0;
        blocked_ = false;
    }

    [[nodiscard]] uint64_t evictions() const { return evictions_; }
    [[nodiscard]] uint64_t blocks() const { return blocks_; }
    [[nodiscard]] uint64_t oversizedBlocks() const { return oversizedBlocks_; }
    [[nodiscard]] uint64_t mipPlanBlocks() const { return mipPlanBlocks_; }
    [[nodiscard]] uint64_t noVictimBlocks() const { return noVictimBlocks_; }
    // True when the most recent decision was a refusal. Cleared by any
    // admission that got as far as looking, which is why it says "right now"
    // rather than "ever".
    [[nodiscard]] bool blocked() const { return blocked_; }

private:
    uint64_t evictions_ = 0;
    uint64_t blocks_ = 0;
    uint64_t oversizedBlocks_ = 0;
    uint64_t mipPlanBlocks_ = 0;
    uint64_t noVictimBlocks_ = 0;
    bool blocked_ = false;
};


// How large a publication could be made to fit: the free headroom plus every
// byte eviction is allowed to reclaim, capped at the limit. Used to pick how
// much of a compressed texture's mip chain to upload, so it answers "how much
// could I have" rather than actually evicting anything.
template <typename Slot, typename ResidentPredicate>
[[nodiscard]] uint64_t evictableCapacity(
    const std::vector<Slot>& slots,
    std::size_t protectedIndex,
    uint64_t visibleRequestStamp,
    ResidentPredicate resident,
    const ResidencyBudget& budget,
    uint64_t limit)
{
    uint64_t capacity = budget.headroom(limit);
    forEachEvictableSlot(
        slots,
        protectedIndex,
        visibleRequestStamp,
        resident,
        [&](std::size_t, const Slot& slot) {
            const uint64_t remaining = limit - capacity;
            capacity += slot.gpuBytes < remaining ? slot.gpuBytes : remaining;
            return capacity != limit;
        });
    return capacity;
}

} // namespace sokoban
