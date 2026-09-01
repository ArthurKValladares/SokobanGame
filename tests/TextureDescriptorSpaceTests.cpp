// Covers how the texture descriptor heap is partitioned.
//
// This arithmetic decided where every sampled texture lives, and it was spread
// across three files: the catalog computed a descriptor index one way, the
// renderer computed it another, and the rule that the two populations must not
// collide was written three times in three wordings. None of it could be
// reached from a test, because two of the three copies sat behind a Vulkan
// device.
//
// The consequence of getting it wrong is not a crash. A manifest index that
// collides with a discovered one means a model samples another model's texture,
// which looks like an authoring mistake.

#include "TestHarness.hpp"

#include "engine/render/TextureDescriptorSpace.hpp"

#include <iostream>

namespace {

using namespace sokoban;
using Space = TextureDescriptorSpace;

void testManifestIndicesArePassedThrough()
{
    TEST("manifestIndicesArePassedThrough");
    // The whole point of the low range: a RenderTexture id written into a level
    // file last session still means the same slot today.
    CHECK(Space::descriptorIndexFor(0, 8, 120) == 0);
    CHECK(Space::descriptorIndexFor(7, 8, 120) == 7);
}

void testDiscoveredIndicesRebaseOntoTheTop()
{
    TEST("discoveredIndicesRebaseOntoTheTop");
    // Logical 8 is the first discovered texture, so it lands on the base.
    CHECK(Space::descriptorIndexFor(8, 8, 120) == 120);
    CHECK(Space::descriptorIndexFor(9, 8, 120) == 121);
    // And they stay in the order they were found.
    CHECK(Space::descriptorIndexFor(11, 8, 120) - Space::descriptorIndexFor(10, 8, 120) == 1);
}

void testTheDiscoveredRangeIsMeasuredFromTheTop()
{
    TEST("theDiscoveredRangeIsMeasuredFromTheTop");
    CHECK(Space::discoveredBaseFor(128, 8) == 120);
    // No discovered textures means the range starts past the end, which is
    // what leaves the whole heap to the manifest.
    CHECK(Space::discoveredBaseFor(128, 0) == 128);
    // A heap given over entirely to discovered textures starts at zero.
    CHECK(Space::discoveredBaseFor(128, 128) == 0);
}

void testTouchingIsNotOverlapping()
{
    TEST("touchingIsNotOverlapping");
    // The manifest range is half-open, so a count equal to the base is exactly
    // full rather than one too many. This is the boundary all three callers
    // test, and the one an off-by-one would move.
    CHECK(!Space::rangesOverlap(120, 120));
    CHECK(Space::rangesOverlap(121, 120));
    CHECK(!Space::rangesOverlap(0, 0));
    CHECK(!Space::rangesOverlap(0, 128));
}

void testResetLaysOutTheHeap()
{
    TEST("resetLaysOutTheHeap");
    Space space;
    space.reset(128, 8, 8);
    CHECK(space.capacity() == 128);
    CHECK(space.manifestCount() == 8);
    CHECK(space.discoveredBase() == 120);
    CHECK(space.manifestHeadroom() == 112);
    CHECK(space.active().empty());
}

void testMembershipFollowsTheManifestCount()
{
    TEST("membershipFollowsTheManifestCount");
    Space space;
    space.reset(128, 8, 8);
    CHECK(space.isManifestTexture(0));
    CHECK(space.isManifestTexture(7));
    CHECK(!space.isManifestTexture(8));
    CHECK(!space.isManifestTexture(120));
    // A heap with no manifest textures owns none of them, including slot zero.
    Space empty;
    empty.reset(128, 0, 4);
    CHECK(!empty.isManifestTexture(0));
}

void testGrowingTheManifestClaimsTheNewSlots()
{
    TEST("growingTheManifestClaimsTheNewSlots");
    Space space;
    space.reset(128, 2, 4);
    space.growManifestRange(5);
    CHECK(space.manifestCount() == 5);
    // Only the newly claimed ones are appended; 0 and 1 were already live.
    CHECK(space.active().size() == 3);
    CHECK(space.active()[0] == 2);
    CHECK(space.active()[2] == 4);
}

void testGrowingBackwardsDoesNothing()
{
    TEST("growingBackwardsDoesNothing");
    // The manifest sync calls this with the manifest's current size, which can
    // equal what is already claimed. It must not shrink the range or re-add.
    Space space;
    space.reset(128, 5, 4);
    space.growManifestRange(5);
    CHECK(space.manifestCount() == 5);
    CHECK(space.active().empty());
    space.growManifestRange(3);
    CHECK(space.manifestCount() == 5);
    CHECK(space.active().empty());
}

void testHeadroomSaturatesWhenTheRangesAlreadyOverlap()
{
    TEST("headroomSaturatesWhenTheRangesAlreadyOverlap");
    // reset() does not reject an overlapping layout - the caller checks and
    // raises its own error - so this has to answer sensibly meanwhile rather
    // than wrapping around to four billion.
    Space space;
    space.reset(16, 12, 8);
    CHECK(space.discoveredBase() == 8);
    CHECK(Space::rangesOverlap(space.manifestCount(), space.discoveredBase()));
    CHECK(space.manifestHeadroom() == 0);
}

void testManifestCanHoldIsTheSameBoundary()
{
    TEST("manifestCanHoldIsTheSameBoundary");
    Space space;
    space.reset(128, 8, 8);
    CHECK(space.manifestCanHold(120));
    CHECK(!space.manifestCanHold(121));
    // Which is the negation of the overlap test, and must stay so.
    CHECK(space.manifestCanHold(120) == !Space::rangesOverlap(120, space.discoveredBase()));
    CHECK(space.manifestCanHold(121) == !Space::rangesOverlap(121, space.discoveredBase()));
}

void testClearForgetsEverything()
{
    TEST("clearForgetsEverything");
    Space space;
    space.reset(128, 8, 8);
    space.markActive(3);
    space.clear();
    CHECK(space.capacity() == 0);
    CHECK(space.manifestCount() == 0);
    CHECK(space.discoveredBase() == 0);
    CHECK(space.active().empty());
}

void testTheInstanceFormAgreesWithTheStaticOne()
{
    TEST("theInstanceFormAgreesWithTheStaticOne");
    Space space;
    space.reset(128, 8, 8);
    for (uint32_t logical = 0; logical < 16; ++logical) {
        CHECK(space.descriptorIndexFor(logical)
            == Space::descriptorIndexFor(logical, 8, 120));
    }
}

void testAFullHeapMapsEveryLogicalIndexExactlyOnce()
{
    TEST("aFullHeapMapsEveryLogicalIndexExactlyOnce");
    // The property that actually matters: no two logical textures may land on
    // the same descriptor. Checked over every split of a small heap.
    const uint32_t capacity = 24;
    for (uint32_t manifestCount = 0; manifestCount <= capacity; ++manifestCount) {
        for (uint32_t discovered = 0;
             discovered <= capacity - manifestCount;
             ++discovered) {
            const uint32_t base = Space::discoveredBaseFor(capacity, discovered);
            if (Space::rangesOverlap(manifestCount, base)) {
                continue;
            }
            std::vector<bool> seen(capacity, false);
            bool collided = false;
            bool escaped = false;
            for (uint32_t logical = 0; logical < manifestCount + discovered;
                 ++logical) {
                const uint32_t slot =
                    Space::descriptorIndexFor(logical, manifestCount, base);
                if (slot >= capacity) { escaped = true; break; }
                if (seen[slot]) { collided = true; break; }
                seen[slot] = true;
            }
            CHECK(!collided);
            CHECK(!escaped);
        }
    }
}

} // namespace

int main()
{
    testManifestIndicesArePassedThrough();
    testDiscoveredIndicesRebaseOntoTheTop();
    testTheDiscoveredRangeIsMeasuredFromTheTop();
    testTouchingIsNotOverlapping();

    testResetLaysOutTheHeap();
    testMembershipFollowsTheManifestCount();
    testGrowingTheManifestClaimsTheNewSlots();
    testGrowingBackwardsDoesNothing();
    testHeadroomSaturatesWhenTheRangesAlreadyOverlap();
    testManifestCanHoldIsTheSameBoundary();
    testClearForgetsEverything();
    testTheInstanceFormAgreesWithTheStaticOne();
    testAFullHeapMapsEveryLogicalIndexExactlyOnce();

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "texture_descriptor_space: " << checks << " checks passed\n";
    return 0;
}
