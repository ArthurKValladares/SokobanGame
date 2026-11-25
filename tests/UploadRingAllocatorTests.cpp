#include "TestHarness.hpp"

#include "engine/render/UploadRingAllocator.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using namespace sokoban;

void testAlignmentAndCompletion()
{
    TEST("alignmentAndCompletion");
    UploadRingAllocator ring(64);
    const auto first = ring.reserve(12, 16);
    const auto second = ring.reserve(8, 16);
    CHECK(first && second);
    CHECK(first->offset == 0);
    CHECK(second->offset == 16);
    ring.commit(*first);
    ring.commit(*second);
    ring.complete(*first);
    CHECK(ring.usedBytes() != 0);
    ring.complete(*second);
    CHECK(ring.usedBytes() == 0);
    CHECK(ring.inFlightCount() == 0);
}

void testOutOfOrderCompletionWaitsForHead()
{
    TEST("outOfOrderCompletionWaitsForHead");
    UploadRingAllocator ring(64);
    const auto first = ring.reserve(20, 4);
    const auto second = ring.reserve(20, 4);
    CHECK(first && second);
    ring.commit(*first);
    ring.commit(*second);
    ring.complete(*second);
    CHECK(ring.inFlightCount() == 2);
    ring.complete(*first);
    CHECK(ring.inFlightCount() == 0);
    CHECK(ring.usedBytes() == 0);
}

void testWrapAndReuse()
{
    TEST("wrapAndReuse");
    UploadRingAllocator ring(32);
    const auto first = ring.reserve(16, 1);
    const auto second = ring.reserve(12, 1);
    CHECK(first && second);
    ring.commit(*first);
    ring.commit(*second);
    ring.complete(*first);
    const auto wrapped = ring.reserve(12, 1);
    CHECK(wrapped.has_value());
    CHECK(wrapped->offset == 0);
}

void testAbandonRestoresLatestReservation()
{
    TEST("abandonRestoresLatestReservation");
    UploadRingAllocator ring(32);
    const auto first = ring.reserve(8, 1);
    const auto second = ring.reserve(8, 1);
    CHECK(first && second);
    ring.abandon(*second);
    const auto reused = ring.reserve(16, 1);
    CHECK(reused.has_value());
    CHECK(reused->offset == 8);
}

void testOversizedReservationIsRejected()
{
    TEST("oversizedReservationIsRejected");
    UploadRingAllocator ring(16);
    CHECK(!ring.reserve(17, 1).has_value());
}

void testCanReservePredictsReserveWithoutTakingSpace()
{
    TEST("canReservePredictsReserveWithoutTakingSpace");
    UploadRingAllocator ring(32);
    CHECK(!ring.canReserve(0, 1));
    CHECK(!ring.canReserve(33, 1));
    CHECK(ring.canReserve(32, 1));
    CHECK(ring.usedBytes() == 0);
    CHECK(ring.inFlightCount() == 0);

    const auto first = ring.reserve(16, 1);
    const auto second = ring.reserve(12, 1);
    CHECK(first && second);
    ring.commit(*first);
    ring.commit(*second);
    // Four bytes remain at the end and the front is still in flight.
    CHECK(ring.canReserve(4, 1));
    CHECK(!ring.canReserve(5, 1));
    // The same four bytes are out of reach once alignment skips past them.
    CHECK(!ring.canReserve(4, 16));
    CHECK(!ring.reserve(4, 16).has_value());

    // Retiring the front upload is what makes a refused request fit, which
    // is why a caller may simply ask again on a later frame.
    ring.complete(*first);
    CHECK(ring.canReserve(12, 1));
    CHECK(!ring.canReserve(17, 1));
    const auto wrapped = ring.reserve(12, 1);
    CHECK(wrapped.has_value());
    CHECK(wrapped->offset == 0);
    CHECK(!ring.canReserve(8, 1));
    CHECK(!ring.reserve(8, 1).has_value());
}

void testCanReserveAgreesWithReserveAcrossAMixedSequence()
{
    TEST("canReserveAgreesWithReserveAcrossAMixedSequence");
    // A deterministic walk through reservations of mixed size and alignment,
    // with completions arriving out of order. The prediction must match what
    // reserve() then does at every step, including around the wrap.
    UploadRingAllocator ring(256);
    std::vector<UploadRingAllocator::Reservation> inFlight;
    uint32_t state = 0x9e3779b9U;
    const auto next = [&state] {
        state = state * 1664525U + 1013904223U;
        return state >> 8;
    };
    int attempts = 0;
    int refusals = 0;
    int disagreements = 0;
    for (int step = 0; step < 4000; ++step) {
        if (!inFlight.empty() && next() % 3 == 0) {
            const std::size_t index = next() % inFlight.size();
            ring.complete(inFlight[index]);
            inFlight.erase(inFlight.begin() +
                static_cast<std::ptrdiff_t>(index));
            continue;
        }
        const uint64_t size = 1 + next() % 96;
        const uint64_t alignment = uint64_t { 1 } << (next() % 5);
        const bool predicted = ring.canReserve(size, alignment);
        const auto reservation = ring.reserve(size, alignment);
        ++attempts;
        refusals += !reservation.has_value();
        disagreements += predicted != reservation.has_value();
        if (reservation) {
            ring.commit(*reservation);
            inFlight.push_back(*reservation);
        }
    }
    // The walk has to reach both answers for agreement to mean anything.
    CHECK(attempts - refusals > 500);
    CHECK(refusals > 500);
    CHECK(disagreements == 0);
}

} // namespace

int main()
{
    testAlignmentAndCompletion();
    testOutOfOrderCompletionWaitsForHead();
    testWrapAndReuse();
    testAbandonRestoresLatestReservation();
    testOversizedReservationIsRejected();
    testCanReservePredictsReserveWithoutTakingSpace();
    testCanReserveAgreesWithReserveAcrossAMixedSequence();
    if (failures == 0) {
        std::cout << "UploadRingAllocatorTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "UploadRingAllocatorTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
