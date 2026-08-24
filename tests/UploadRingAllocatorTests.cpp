#include "engine/render/UploadRingAllocator.hpp"

#include <iostream>
#include <stdexcept>

namespace {

using namespace sokoban;

int failures = 0;
int checks = 0;
const char* currentTest = "";

void checkImpl(bool ok, const char* expression, int line)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << "FAIL [" << currentTest << "] line " << line
                  << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)
#define TEST(name) currentTest = name

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

} // namespace

int main()
{
    testAlignmentAndCompletion();
    testOutOfOrderCompletionWaitsForHead();
    testWrapAndReuse();
    testAbandonRestoresLatestReservation();
    testOversizedReservationIsRejected();
    if (failures == 0) {
        std::cout << "UploadRingAllocatorTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "UploadRingAllocatorTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
