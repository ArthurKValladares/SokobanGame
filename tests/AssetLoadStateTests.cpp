// Covers the load-state machine every asset kind runs.
//
// The gate's behaviour was checked once, by hand, over every load state and
// both values of `wait` - the comment on it says "forty reachable cases, all
// three reproduced exactly" - and then nothing pinned it, because it lived in
// a private template of a class that needs a Vulkan device to construct. It is
// a free function now, so the forty cases are a table.

#include "TestHarness.hpp"

#include "engine/render/AssetLoadState.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace sokoban;

// The four fields the machine reads. ModelSlot, TextureSlot and AnimationSlot
// all have them and disagree about everything else.
struct Slot {
    LoadState state = LoadState::Unrequested;
    std::exception_ptr failure;
};

const std::filesystem::path kPath = "assets/thing.gltf";

std::exception_ptr failureOf(const char* what)
{
    try {
        throw std::runtime_error(what);
    } catch (...) {
        return std::current_exception();
    }
}

const char* name(LoadState state)
{
    switch (state) {
    case LoadState::Unrequested: return "Unrequested";
    case LoadState::Queued: return "Queued";
    case LoadState::Loading: return "Loading";
    case LoadState::CpuReady: return "CpuReady";
    case LoadState::Uploading: return "Uploading";
    case LoadState::Ready: return "Ready";
    case LoadState::Failed: return "Failed";
    }
    return "?";
}

// ------------------------------------------------------------- the gate

// Every state, both values of wait, written out rather than computed - a table
// that agrees with the code by construction would prove nothing.
void testGateOverEveryStateAndBothWaits()
{
    TEST("gateOverEveryStateAndBothWaits");
    struct Case {
        LoadState state;
        PublishGate expected;
    };
    // Proceed means "there is work a publication could do now". Only Loading
    // and CpuReady qualify: everything before them has not been asked for yet,
    // and everything after is already in flight or done.
    const Case cases[] = {
        { LoadState::Unrequested, PublishGate::Stop },
        { LoadState::Queued, PublishGate::Stop },
        { LoadState::Loading, PublishGate::Proceed },
        { LoadState::CpuReady, PublishGate::Proceed },
        { LoadState::Uploading, PublishGate::Stop },
        { LoadState::Ready, PublishGate::Stop },
    };
    for (const Case& one : cases) {
        for (const bool wait : { false, true }) {
            Slot slot { one.state, {} };
            const PublishGate gate = publishGate(slot, kPath, "model", wait);
            if (gate != one.expected) {
                std::cerr << "  state " << name(one.state)
                          << " wait=" << wait << '\n';
            }
            CHECK(gate == one.expected);
        }
    }
}

void testUploadingStopsForTheSameReasonReadyDoes()
{
    TEST("uploadingStopsForTheSameReasonReadyDoes");
    // Its bytes are already charged and its fence is already in flight, so a
    // second attempt has nothing useful to do. This is the case the three
    // hand-written copies disagreed about in shape: one fell through and
    // returned false further down, one rejected it here.
    Slot slot { LoadState::Uploading, {} };
    CHECK(publishGate(slot, kPath, "texture", false) == PublishGate::Stop);
    CHECK(publishGate(slot, kPath, "texture", true) == PublishGate::Stop);
}

void testFailedStopsQuietlyUnlessSomeoneIsWaiting()
{
    TEST("failedStopsQuietlyUnlessSomeoneIsWaiting");
    Slot slot { LoadState::Failed, failureOf("disk on fire") };

    // Nobody waiting: the caller is told to stop and the failure stays stored.
    CHECK(publishGate(slot, kPath, "model", false) == PublishGate::Stop);

    // Someone waiting: the stored failure is rethrown, named, at the call.
    bool threw = false;
    try {
        (void)publishGate(slot, kPath, "model", true);
    } catch (const std::runtime_error& error) {
        threw = true;
        const std::string_view what = error.what();
        CHECK(what.find("model") != std::string_view::npos);
        CHECK(what.find("assets/thing.gltf") != std::string_view::npos);
        CHECK(what.find("disk on fire") != std::string_view::npos);
    }
    CHECK(threw);
}

void testAFailedSlotWithNoStoredExceptionStillThrows()
{
    TEST("aFailedSlotWithNoStoredExceptionStillThrows");
    // The state is the authority, not the exception pointer: a slot marked
    // Failed must never look publishable just because nothing was captured.
    Slot slot { LoadState::Failed, {} };
    CHECK(publishGate(slot, kPath, "animation", false) == PublishGate::Stop);
    bool threw = false;
    try {
        (void)publishGate(slot, kPath, "animation", true);
    } catch (const std::runtime_error& error) {
        threw = true;
        CHECK(std::string_view(error.what()).find("animation") !=
            std::string_view::npos);
    }
    CHECK(threw);
}

// -------------------------------------------------------- throwIfFailed

void testThrowIfFailedIgnoresEveryOtherState()
{
    TEST("throwIfFailedIgnoresEveryOtherState");
    for (const LoadState state : {
             LoadState::Unrequested,
             LoadState::Queued,
             LoadState::Loading,
             LoadState::CpuReady,
             LoadState::Uploading,
             LoadState::Ready,
         }) {
        bool threw = false;
        try {
            // A stored failure is deliberately present: a slot that failed and
            // was then reset must not keep throwing.
            throwIfFailed(state, failureOf("stale"), kPath, "model");
        } catch (...) {
            threw = true;
        }
        if (threw) {
            std::cerr << "  state " << name(state) << " threw\n";
        }
        CHECK(!threw);
    }
}

// --------------------------------------------------- recording a failure

void testRecordingAFailureMarksTheSlotAndKeepsTheException()
{
    TEST("recordingAFailureMarksTheSlotAndKeepsTheException");
    Slot slot { LoadState::Loading, {} };
    try {
        throw std::runtime_error("decode failed");
    } catch (...) {
        recordPublishFailure(slot, kPath, "texture", "preparation", false);
    }
    CHECK(slot.state == LoadState::Failed);
    CHECK(slot.failure != nullptr);

    // And the gate now agrees the slot is done.
    CHECK(publishGate(slot, kPath, "texture", false) == PublishGate::Stop);
}

void testRecordingAFailureRethrowsForACallerThatIsWaiting()
{
    TEST("recordingAFailureRethrowsForACallerThatIsWaiting");
    Slot slot { LoadState::Loading, {} };
    bool threw = false;
    try {
        try {
            throw std::runtime_error("decode failed");
        } catch (...) {
            recordPublishFailure(slot, kPath, "texture", "upload", true);
        }
    } catch (const std::runtime_error& error) {
        threw = true;
        const std::string_view what = error.what();
        CHECK(what.find("texture") != std::string_view::npos);
        CHECK(what.find("decode failed") != std::string_view::npos);
    }
    CHECK(threw);
    // The slot is marked before the rethrow, so a waiting caller that catches
    // does not leave the slot looking loadable.
    CHECK(slot.state == LoadState::Failed);
}

} // namespace

int main()
{
    testGateOverEveryStateAndBothWaits();
    testUploadingStopsForTheSameReasonReadyDoes();
    testFailedStopsQuietlyUnlessSomeoneIsWaiting();
    testAFailedSlotWithNoStoredExceptionStillThrows();
    testThrowIfFailedIgnoresEveryOtherState();
    testRecordingAFailureMarksTheSlotAndKeepsTheException();
    testRecordingAFailureRethrowsForACallerThatIsWaiting();

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "asset_load_state: " << checks << " checks passed\n";
    return 0;
}
