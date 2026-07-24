#include "engine/render/FrameResourceTracker.hpp"

#include <iostream>
#include <stdexcept>

namespace {

int failures = 0;
int checks = 0;

void checkImpl(bool condition, const char* expression, int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": "
                  << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)

template <typename Exception, typename Function>
void checkThrows(Function function, int line)
{
    ++checks;
    try {
        function();
    } catch (const Exception&) {
        return;
    } catch (...) {
    }
    ++failures;
    std::cerr << "FAIL line " << line
              << ": expected exception\n";
}

#define CHECK_THROWS(type, expression) \
    checkThrows<type>([&] { expression; }, __LINE__)

void testTracksOverlappingGenerationsExactly()
{
    sokoban::FrameResourceTracker tracker(2);
    tracker.markSubmitted(0, 10);
    tracker.markSubmitted(1, 10);
    CHECK(tracker.pendingMask() == 0b11);
    CHECK(tracker.pendingMaskForGeneration(10) == 0b11);

    CHECK(tracker.complete(0));
    tracker.markSubmitted(0, 11);
    CHECK(tracker.pendingMask() == 0b11);
    CHECK(tracker.pendingMaskForGeneration(10) == 0b10);
    CHECK(tracker.pendingMaskForGeneration(11) == 0b01);

    CHECK(tracker.complete(1));
    CHECK(tracker.pendingMask() == 0b01);
    CHECK(!tracker.complete(1));
    CHECK(tracker.complete(0));
    CHECK(tracker.pendingMask() == 0);
}

void testRejectsInvalidSubmissionTransitions()
{
    sokoban::FrameResourceTracker tracker(2);
    CHECK_THROWS(std::invalid_argument,
        tracker.markSubmitted(0, 0));
    tracker.markSubmitted(0, 1);
    CHECK_THROWS(std::logic_error,
        tracker.markSubmitted(0, 2));
    CHECK_THROWS(std::out_of_range,
        tracker.markSubmitted(2, 1));
}

} // namespace

int main()
{
    testTracksOverlappingGenerationsExactly();
    testRejectsInvalidSubmissionTransitions();

    if (failures == 0) {
        std::cout << "FrameResourceTrackerTests: "
                  << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "FrameResourceTrackerTests: "
              << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
