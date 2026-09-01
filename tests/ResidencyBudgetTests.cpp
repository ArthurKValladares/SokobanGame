// Covers the residency accounting and eviction policy extracted from
// VulkanModelResources. None of this could be reached from a test before: it
// lived in two private methods that needed a Vulkan device to call.
//
// The cases below are written against the behaviour those methods had, not
// against what the policy arguably should be. Where a rule looks surprising -
// eviction refusing to run while a retirement is pending, a publication that
// evicted its own victims still failing - there is a test saying so, because
// those are the rules the fence-owned retirement contract depends on.

#include "TestHarness.hpp"

#include "engine/render/ResidencyBudget.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

namespace {

using namespace sokoban;

// Stands in for ModelSlot and TextureSlot, which agree on the three fields the
// policy reads. `ready` plays the part of their LoadState.
struct Slot {
    bool ready = false;
    uint64_t gpuBytes = 0;
    uint64_t lastRequested = 0;
};

constexpr auto isReady = [](const Slot& slot) { return slot.ready; };

std::optional<std::size_t> victimOf(
    const std::vector<Slot>& slots,
    std::size_t protectedIndex = static_cast<std::size_t>(-1),
    uint64_t stamp = 0)
{
    return chooseResidencyVictim(slots, protectedIndex, stamp, isReady);
}

// ----------------------------------------------------------- accounting

void testAccounting()
{
    TEST("accounting");
    ResidencyBudget budget;
    CHECK(budget.resident() == 0);
    CHECK(budget.retiring() == 0);
    CHECK(budget.peak() == 0);

    budget.addResident(100);
    budget.addResident(50);
    CHECK(budget.resident() == 150);
    CHECK(budget.peak() == 150);

    // Retiring does not reduce residency: the memory is still on the device
    // until the fences that could reference it clear.
    budget.beginRetiring(50);
    CHECK(budget.resident() == 150);
    CHECK(budget.retiring() == 50);
    CHECK(budget.retirementPending());

    budget.finishRetiring(50);
    CHECK(budget.resident() == 100);
    CHECK(budget.retiring() == 0);
    CHECK(!budget.retirementPending());
    // The peak is a high-water mark and does not walk back down.
    CHECK(budget.peak() == 150);

    budget.reset();
    CHECK(budget.resident() == 0 && budget.retiring() == 0 && budget.peak() == 0);
}

void testInPlaceReplacementTracksBothDirections()
{
    TEST("inPlaceReplacementTracksBothDirections");
    // The editor repainting a splat map is the only path that swaps a resource
    // without retiring it, and the new image may be larger or smaller.
    ResidencyBudget budget;
    budget.addResident(1000);
    budget.replaceResident(400, 900);
    CHECK(budget.resident() == 1500);
    CHECK(budget.peak() == 1500);

    budget.replaceResident(900, 100);
    CHECK(budget.resident() == 700);
    CHECK(budget.peak() == 1500);
}

void testFitAndHeadroom()
{
    TEST("fitAndHeadroom");
    ResidencyBudget budget;
    budget.addResident(600);

    CHECK(budget.fits(400, 1000));
    CHECK(!budget.fits(401, 1000));
    CHECK(budget.headroom(1000) == 400);

    // Retiring bytes are not free space yet, but they are excluded from the
    // headroom calculation because eviction already spoke for them.
    budget.beginRetiring(200);
    CHECK(budget.headroom(1000) == 600);
    // fits() still measures against real residency, so it is unmoved.
    CHECK(!budget.fits(401, 1000));

    // Over the limit: headroom saturates instead of wrapping around zero,
    // and nothing fits at all - not even a zero-byte request.
    ResidencyBudget over;
    over.addResident(2000);
    CHECK(over.headroom(1000) == 0);
    CHECK(!over.fits(0, 1000));
}

void testNeedsEvictionCountsScheduledAndRetiring()
{
    TEST("needsEvictionCountsScheduledAndRetiring");
    ResidencyBudget budget;
    budget.addResident(900);

    CHECK(budget.needsEviction(200, 0, 1000));
    // Scheduling 200 bytes of victims makes room for the request.
    CHECK(!budget.needsEviction(200, 200, 1000));
    // Bytes already retiring count the same way, and are not double-counted
    // with newly scheduled ones.
    budget.beginRetiring(100);
    CHECK(!budget.needsEviction(200, 100, 1000));
    // Landing exactly on the limit is a fit, not a reason to evict.
    CHECK(!budget.needsEviction(300, 100, 1000));
    CHECK(budget.needsEviction(301, 100, 1000));
}

// ------------------------------------------------------ victim selection

void testVictimIsLeastRecentlyRequested()
{
    TEST("victimIsLeastRecentlyRequested");
    const std::vector<Slot> slots {
        { true, 10, 5 },
        { true, 10, 2 },
        { true, 10, 9 },
    };
    CHECK(victimOf(slots) == std::optional<std::size_t> { 1 });
}

void testVictimSkipsIneligibleSlots()
{
    TEST("victimSkipsIneligibleSlots");
    // Only index 3 is eligible: 0 is not resident, 1 holds no bytes, 2 is the
    // asset being published.
    const std::vector<Slot> slots {
        { false, 10, 1 },
        { true, 0, 1 },
        { true, 10, 1 },
        { true, 10, 7 },
    };
    CHECK(chooseResidencyVictim(slots, 2, 0, isReady)
        == std::optional<std::size_t> { 3 });
}

void testVictimSkipsAssetsRequestedThisFrame()
{
    TEST("victimSkipsAssetsRequestedThisFrame");
    // Stamp 4 is the frame being prepared. Index 0 was requested for it, so
    // evicting it would only force an immediate reload.
    const std::vector<Slot> slots {
        { true, 10, 4 },
        { true, 10, 3 },
    };
    CHECK(chooseResidencyVictim(slots, 99, 4, isReady)
        == std::optional<std::size_t> { 1 });

    // With every candidate wanted this frame there is nothing to give up.
    const std::vector<Slot> allWanted { { true, 10, 4 }, { true, 10, 4 } };
    CHECK(!chooseResidencyVictim(allWanted, 99, 4, isReady).has_value());
}

void testStampZeroProtectsNothing()
{
    TEST("stampZeroProtectsNothing");
    // A zero stamp means no visible request has been made yet - during the
    // blocking offline path, for instance - so lastRequested == 0 slots stay
    // eligible rather than every one of them being treated as wanted.
    const std::vector<Slot> slots { { true, 10, 0 }, { true, 10, 0 } };
    CHECK(victimOf(slots, static_cast<std::size_t>(-1), 0)
        == std::optional<std::size_t> { 0 });
}

void testNoVictimWhenNothingIsEvictable()
{
    TEST("noVictimWhenNothingIsEvictable");
    CHECK(!victimOf({}).has_value());
    const std::vector<Slot> loading { { false, 10, 1 } };
    CHECK(!victimOf(loading).has_value());
}

// ----------------------------------------------------------- capacity

void testEvictableCapacity()
{
    TEST("evictableCapacity");
    ResidencyBudget budget;
    budget.addResident(800);
    const std::vector<Slot> slots {
        { true, 100, 1 },
        { true, 200, 2 },
        { false, 400, 3 },
    };
    // 200 free, plus the 300 bytes the two resident slots could give up.
    CHECK(evictableCapacity(slots, static_cast<std::size_t>(-1), 0, isReady,
              budget, 1000)
        == 500);

    // Protecting a slot removes its bytes from what could be reclaimed.
    CHECK(evictableCapacity(slots, 1, 0, isReady, budget, 1000) == 300);
}

void testEvictableCapacityStopsAtTheLimit()
{
    TEST("evictableCapacityStopsAtTheLimit");
    ResidencyBudget budget;
    budget.addResident(500);
    const std::vector<Slot> slots {
        { true, 400, 1 },
        { true, 400, 2 },
        { true, 400, 3 },
    };
    // 500 free plus 1200 evictable would overshoot; capacity is the limit.
    CHECK(evictableCapacity(slots, static_cast<std::size_t>(-1), 0, isReady,
              budget, 1000)
        == 1000);
}

// ------------------------------------------------- the composed decision

// Mirrors makeModelResident/makeTextureResident so the extracted pieces are
// exercised in the order the renderer uses them.
struct Outcome {
    bool admitted = false;
    bool blocked = false;
    std::size_t evicted = 0;
};

Outcome admit(
    ResidencyBudget& budget,
    std::vector<Slot>& slots,
    std::size_t protectedIndex,
    uint64_t stamp,
    uint64_t bytes,
    uint64_t limit)
{
    Outcome outcome;
    if (bytes > limit) {
        outcome.blocked = true;
        return outcome;
    }
    if (budget.fits(bytes, limit)) {
        outcome.admitted = true;
        return outcome;
    }
    if (budget.retirementPending()) {
        return outcome;
    }
    uint64_t scheduled = 0;
    while (budget.needsEviction(bytes, scheduled, limit)) {
        const std::optional<std::size_t> victim =
            chooseResidencyVictim(slots, protectedIndex, stamp, isReady);
        if (!victim) {
            outcome.blocked = true;
            return outcome;
        }
        scheduled += slots[*victim].gpuBytes;
        budget.beginRetiring(slots[*victim].gpuBytes);
        slots[*victim] = {};
        ++outcome.evicted;
    }
    outcome.admitted = budget.fits(bytes, limit);
    return outcome;
}

void testLadderAdmitsWhenItAlreadyFits()
{
    TEST("ladderAdmitsWhenItAlreadyFits");
    ResidencyBudget budget;
    budget.addResident(100);
    std::vector<Slot> slots { { true, 100, 1 } };
    const Outcome outcome = admit(budget, slots, 99, 0, 200, 1000);
    CHECK(outcome.admitted);
    CHECK(outcome.evicted == 0);
}

void testLadderRefusesARequestLargerThanTheWholeBudget()
{
    TEST("ladderRefusesARequestLargerThanTheWholeBudget");
    ResidencyBudget budget;
    std::vector<Slot> slots { { true, 100, 1 } };
    const Outcome outcome = admit(budget, slots, 99, 0, 2000, 1000);
    CHECK(!outcome.admitted);
    CHECK(outcome.blocked);
    CHECK(outcome.evicted == 0);
}

void testLadderEvictsThenStillWaitsForTheFence()
{
    TEST("ladderEvictsThenStillWaitsForTheFence");
    // The rule that keeps the hard budget honest: choosing victims does not
    // free their bytes, so the publication that evicted them is refused too
    // and retries after the fence clears.
    ResidencyBudget budget;
    budget.addResident(900);
    std::vector<Slot> slots { { true, 500, 1 }, { true, 400, 2 } };

    const Outcome first = admit(budget, slots, 99, 0, 300, 1000);
    CHECK(first.evicted == 1);
    CHECK(!first.admitted);
    CHECK(budget.retiring() == 500);
    CHECK(budget.resident() == 900);

    // The fence clears and the memory is actually released.
    budget.finishRetiring(500);
    CHECK(budget.resident() == 400);

    const Outcome second = admit(budget, slots, 99, 0, 300, 1000);
    CHECK(second.admitted);
    CHECK(second.evicted == 0);
}

void testLadderWillNotCascadeWhileARetirementIsPending()
{
    TEST("ladderWillNotCascadeWhileARetirementIsPending");
    // A second asset asking for room while the first eviction is still in
    // flight must wait rather than choosing more victims of its own.
    ResidencyBudget budget;
    budget.addResident(900);
    budget.beginRetiring(500);
    std::vector<Slot> slots { { true, 400, 2 } };

    const Outcome outcome = admit(budget, slots, 99, 0, 300, 1000);
    CHECK(!outcome.admitted);
    CHECK(!outcome.blocked);
    CHECK(outcome.evicted == 0);
    CHECK(slots[0].ready);
}

void testLadderBlocksWhenNothingCanBeGivenUp()
{
    TEST("ladderBlocksWhenNothingCanBeGivenUp");
    ResidencyBudget budget;
    budget.addResident(900);
    // Everything resident is wanted by the frame being prepared.
    std::vector<Slot> slots { { true, 400, 7 }, { true, 500, 7 } };
    const Outcome outcome = admit(budget, slots, 99, 7, 300, 1000);
    CHECK(!outcome.admitted);
    CHECK(outcome.blocked);
    CHECK(outcome.evicted == 0);
}

void testLadderEvictsSeveralVictimsWhenOneIsNotEnough()
{
    TEST("ladderEvictsSeveralVictimsWhenOneIsNotEnough");
    ResidencyBudget budget;
    budget.addResident(1000);
    std::vector<Slot> slots {
        { true, 100, 3 },
        { true, 100, 1 },
        { true, 100, 2 },
    };
    const Outcome outcome = admit(budget, slots, 99, 0, 250, 1000);
    // Least recently requested first: stamp 1, then stamp 2.
    CHECK(outcome.evicted >= 2);
    CHECK(!slots[1].ready);
    CHECK(!slots[2].ready);
}

// Pins a quirk of the loop this policy was extracted from, so that changing it
// is a deliberate act rather than an accident.
//
// Choosing a victim raises `retiring` (retireModel does that) *and* adds the
// same bytes to the loop's own `scheduled` tally, so the stop condition
// subtracts each victim twice. Eviction therefore stops after freeing about
// half of what the publication asked for, the publication is refused, and it
// retries after the fence clears. The hard limit is never exceeded - this
// under-evicts, it does not oversubscribe - but an asset takes more rounds to
// become resident under pressure than the code reads as intending.
void testEvictionStopsEarlyBecauseVictimsAreCountedTwice()
{
    TEST("evictionStopsEarlyBecauseVictimsAreCountedTwice");
    ResidencyBudget budget;
    budget.addResident(1000);
    std::vector<Slot> slots {
        { true, 100, 1 },
        { true, 100, 2 },
        { true, 100, 3 },
    };
    // 250 bytes are needed; two victims (200 bytes) is where it stops.
    const Outcome outcome = admit(budget, slots, 99, 0, 250, 1000);
    CHECK(outcome.evicted == 2);
    CHECK(budget.retiring() == 200);
    CHECK(!outcome.admitted);
    CHECK(!outcome.blocked);
    // Counting each victim once would have taken a third and admitted it.
    CHECK(budget.resident() + 250 > 1000);
}

} // namespace

int main()
{
    testAccounting();
    testInPlaceReplacementTracksBothDirections();
    testFitAndHeadroom();
    testNeedsEvictionCountsScheduledAndRetiring();

    testVictimIsLeastRecentlyRequested();
    testVictimSkipsIneligibleSlots();
    testVictimSkipsAssetsRequestedThisFrame();
    testStampZeroProtectsNothing();
    testNoVictimWhenNothingIsEvictable();

    testEvictableCapacity();
    testEvictableCapacityStopsAtTheLimit();

    testLadderAdmitsWhenItAlreadyFits();
    testLadderRefusesARequestLargerThanTheWholeBudget();
    testLadderEvictsThenStillWaitsForTheFence();
    testLadderWillNotCascadeWhileARetirementIsPending();
    testLadderBlocksWhenNothingCanBeGivenUp();
    testLadderEvictsSeveralVictimsWhenOneIsNotEnough();
    testEvictionStopsEarlyBecauseVictimsAreCountedTwice();

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "residency_budget: " << checks << " checks passed\n";
    return 0;
}
