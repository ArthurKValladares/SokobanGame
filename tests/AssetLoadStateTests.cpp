#include "TestHarness.hpp"

#include "engine/render/AssetLoadState.hpp"

#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace sokoban;

using Publication = PreparedAssetPublication<std::string>;

const std::filesystem::path kPath = "assets/thing.gltf";

template <typename Action>
bool throwsLogicError(Action&& action)
{
    try {
        action();
    } catch (const std::logic_error&) {
        return true;
    }
    return false;
}

void prepare(Publication& publication, std::string payload = "decoded")
{
    CHECK(publication.queue());
    std::promise<std::string> promise;
    publication.startDecoding(promise.get_future());
    promise.set_value(std::move(payload));
    CHECK(publication.readyForPublication());
    publication.collectDecoded(
        64,
        "Fixture",
        [](const std::string& value) {
            return static_cast<uint64_t>(value.size());
        });
}

void testDecodeUploadAndRetirementLifecycle()
{
    TEST("decodeUploadAndRetirementLifecycle");
    Publication publication;
    CHECK(publication.state() == LoadState::Unrequested);
    CHECK(publication.gate(kPath, "model", false) == PublishGate::Stop);

    CHECK(publication.queue());
    CHECK(publication.queue());
    CHECK(publication.state() == LoadState::Queued);
    CHECK(publication.gate(kPath, "model", false) == PublishGate::Stop);

    std::promise<std::string> promise;
    publication.startDecoding(promise.get_future());
    CHECK(publication.state() == LoadState::Loading);
    CHECK(!publication.canCollectDecoded(false));
    CHECK(publication.gate(kPath, "model", false) == PublishGate::Proceed);

    promise.set_value("decoded");
    CHECK(publication.canCollectDecoded(false));
    publication.collectDecoded(
        64,
        "Fixture",
        [](const std::string& value) {
            return static_cast<uint64_t>(value.size());
        });
    CHECK(publication.state() == LoadState::CpuReady);
    CHECK(publication.prepared() == "decoded");
    CHECK(publication.preparedBytes() == 7);

    publication.beginUpload();
    CHECK(publication.state() == LoadState::Uploading);
    CHECK(publication.preparedBytes() == 0);
    CHECK(publication.gate(kPath, "model", false) == PublishGate::Stop);

    publication.finishUpload();
    CHECK(publication.state() == LoadState::Ready);
    publication.retireResident();
    CHECK(publication.state() == LoadState::Unrequested);
}

void testAdmissionRetryRetainsOnePreparedPayload()
{
    TEST("admissionRetryRetainsOnePreparedPayload");
    Publication publication;
    prepare(publication, "retry me");

    const std::string* const firstAttempt = &publication.prepared();
    CHECK(publication.state() == LoadState::CpuReady);
    const std::string* const secondAttempt = &publication.prepared();
    CHECK(firstAttempt == secondAttempt);
    CHECK(*secondAttempt == "retry me");

    publication.beginUpload();
    CHECK(throwsLogicError([&] { (void)publication.prepared(); }));
}

void testAnimationCanPublishWithoutAnUploadPhase()
{
    TEST("animationCanPublishWithoutAnUploadPhase");
    Publication publication;
    prepare(publication);
    publication.publishResident();
    CHECK(publication.state() == LoadState::Ready);
    CHECK(publication.preparedBytes() == 0);
}

void testQueuedCancellationReturnsToUnrequested()
{
    TEST("queuedCancellationReturnsToUnrequested");
    Publication publication;
    CHECK(publication.queue());
    publication.cancelQueued();
    CHECK(publication.state() == LoadState::Unrequested);
    CHECK(publication.queue());
}

void testDecodeFailureIsStoredAndRethrownForAWaiter()
{
    TEST("decodeFailureIsStoredAndRethrownForAWaiter");
    Publication publication;
    CHECK(publication.queue());
    std::promise<std::string> promise;
    publication.startDecoding(promise.get_future());
    promise.set_exception(
        std::make_exception_ptr(std::runtime_error("disk on fire")));
    try {
        publication.collectDecoded(
            64,
            "Fixture",
            [](const std::string& value) {
                return static_cast<uint64_t>(value.size());
            });
    } catch (...) {
        publication.fail(kPath, "texture", "preparation", false);
    }
    CHECK(publication.state() == LoadState::Failed);
    CHECK(publication.gate(kPath, "texture", false) == PublishGate::Stop);

    bool threw = false;
    try {
        (void)publication.gate(kPath, "texture", true);
    } catch (const std::runtime_error& error) {
        threw = true;
        const std::string_view what = error.what();
        CHECK(what.find("texture") != std::string_view::npos);
        CHECK(what.find("assets/thing.gltf") != std::string_view::npos);
        CHECK(what.find("disk on fire") != std::string_view::npos);
    }
    CHECK(threw);
}

void testBlockingFailureMarksFailedBeforeRethrowing()
{
    TEST("blockingFailureMarksFailedBeforeRethrowing");
    Publication publication;
    CHECK(publication.queue());
    std::promise<std::string> promise;
    publication.startDecoding(promise.get_future());
    promise.set_exception(
        std::make_exception_ptr(std::runtime_error("decode rejected")));

    bool threw = false;
    try {
        try {
            publication.collectDecoded(
                64,
                "Fixture",
                [](const std::string& value) {
                    return static_cast<uint64_t>(value.size());
                });
        } catch (...) {
            publication.fail(kPath, "model", "preparation", true);
        }
    } catch (const std::runtime_error& error) {
        threw = true;
        CHECK(publication.state() == LoadState::Failed);
        const std::string_view what = error.what();
        CHECK(what.find("decode rejected") != std::string_view::npos);
    }
    CHECK(threw);
}

void testPreparedEstimateFailureReleasesThePayload()
{
    TEST("preparedEstimateFailureReleasesThePayload");
    Publication publication;
    CHECK(publication.queue());
    std::promise<std::string> promise;
    publication.startDecoding(promise.get_future());
    promise.set_value("too large");
    try {
        publication.collectDecoded(
            1,
            "Fixture",
            [](const std::string& value) {
                return static_cast<uint64_t>(value.size());
            });
    } catch (...) {
        publication.fail(kPath, "model", "preparation", false);
    }
    CHECK(publication.state() == LoadState::Failed);
    CHECK(publication.preparedBytes() == 0);
    CHECK(throwsLogicError([&] { (void)publication.prepared(); }));
}

void testInvalidTransitionsFailImmediately()
{
    TEST("invalidTransitionsFailImmediately");
    Publication publication;
    CHECK(throwsLogicError([&] { publication.beginUpload(); }));
    CHECK(throwsLogicError([&] { publication.finishUpload(); }));
    CHECK(throwsLogicError([&] { publication.retireResident(); }));
    CHECK(throwsLogicError([&] { publication.cancelQueued(); }));
    CHECK(throwsLogicError([&] {
        publication.startDecoding(std::future<std::string> {});
    }));
}

} // namespace

int main()
{
    testDecodeUploadAndRetirementLifecycle();
    testAdmissionRetryRetainsOnePreparedPayload();
    testAnimationCanPublishWithoutAnUploadPhase();
    testQueuedCancellationReturnsToUnrequested();
    testDecodeFailureIsStoredAndRethrownForAWaiter();
    testBlockingFailureMarksFailedBeforeRethrowing();
    testPreparedEstimateFailureReleasesThePayload();
    testInvalidTransitionsFailImmediately();

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "asset_load_state: " << checks << " checks passed\n";
    return 0;
}
