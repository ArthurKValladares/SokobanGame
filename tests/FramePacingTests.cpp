#include "TestHarness.hpp"

#include "engine/PresentationPolicy.hpp"
#include "engine/Time.hpp"

#include <chrono>
#include <iostream>

namespace {

void testPresentationPolicyUsesOnlyRequestedSafeModes()
{
    using namespace sokoban;
    constexpr PresentationModeSupport fullSupport {
        .fifo = true,
        .mailbox = true,
        .immediate = true,
    };
    CHECK(choosePresentationMode(fullSupport, { .vsync = true }) ==
        PresentationMode::Fifo);
    CHECK(choosePresentationMode(fullSupport, {
        .vsync = false, .allowTearing = false }) == PresentationMode::Mailbox);
    CHECK(choosePresentationMode({ .fifo = true, .immediate = true }, {
        .vsync = false, .allowTearing = false }) == PresentationMode::Fifo);
    CHECK(choosePresentationMode({ .fifo = true, .immediate = true }, {
        .vsync = false, .allowTearing = true }) == PresentationMode::Immediate);
    CHECK(choosePresentationMode({}, {
        .vsync = false, .allowTearing = true }) == PresentationMode::Fifo);
}

void testFramePacerPrioritizesLifecycleThrottles()
{
    using namespace sokoban;
    FramePacer pacer;
    pacer.setFrameRateLimit(120);
    CHECK(pacer.activity() == FramePacingActivity::Focused);
    CHECK(FramePacer::targetInterval(pacer.activity(), pacer.frameRateLimit()) ==
        std::chrono::nanoseconds { 1'000'000'000LL / 120 });

    pacer.setSuspended(FramePacingSuspension::Unfocused, true);
    CHECK(pacer.activity() == FramePacingActivity::Unfocused);
    CHECK(FramePacer::targetInterval(pacer.activity(), pacer.frameRateLimit()) ==
        std::chrono::milliseconds { 50 });

    pacer.setSuspended(FramePacingSuspension::Minimized, true);
    CHECK(pacer.activity() == FramePacingActivity::Minimized);
    CHECK(FramePacer::targetInterval(pacer.activity(), pacer.frameRateLimit()) ==
        std::chrono::milliseconds { 200 });

    pacer.setSuspended(FramePacingSuspension::Backgrounded, true);
    CHECK(pacer.activity() == FramePacingActivity::Backgrounded);
    pacer.setSuspended(FramePacingSuspension::Backgrounded, false);
    CHECK(pacer.activity() == FramePacingActivity::Minimized);
    pacer.setSuspended(FramePacingSuspension::Minimized, false);
    CHECK(pacer.activity() == FramePacingActivity::Unfocused);
    pacer.setSuspended(FramePacingSuspension::Unfocused, false);
    CHECK(pacer.activity() == FramePacingActivity::Focused);
}

void testUnlimitedForegroundDoesNotBusyWaitWhenThrottled()
{
    using namespace sokoban;
    CHECK(FramePacer::targetInterval(FramePacingActivity::Focused, 0) ==
        std::chrono::nanoseconds::zero());
    CHECK(FramePacer::targetInterval(FramePacingActivity::Unfocused, 0) ==
        std::chrono::milliseconds { 50 });
    CHECK(FramePacer::targetInterval(FramePacingActivity::Minimized, 0) ==
        std::chrono::milliseconds { 200 });
}

} // namespace

int main()
{
    testPresentationPolicyUsesOnlyRequestedSafeModes();
    testFramePacerPrioritizesLifecycleThrottles();
    testUnlimitedForegroundDoesNotBusyWaitWhenThrottled();

    if (failures == 0) {
        std::cout << "FramePacingTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "FramePacingTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
