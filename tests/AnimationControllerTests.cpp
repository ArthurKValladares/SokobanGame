// Headless tests for animation selection, preview, deduplication, and fades.

#include "engine/render/AnimationController.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

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
        std::cerr << "FAIL [" << currentTest << "] line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)
#define TEST(name) currentTest = name

bool near(float left, float right)
{
    return std::abs(left - right) < 0.0001f;
}

GltfAnimationClip makeClip(std::string name)
{
    GltfAnimationClip clip;
    clip.name = std::move(name);
    clip.channels.emplace_back();
    return clip;
}

RenderFrameData frameWithAnimation(RenderAnimation animation, float timeSeconds)
{
    RenderFrameData frame;
    frame.tiles.push_back({
        .model = RenderModel::Rogue,
        .animation = animation,
        .animationTimeSeconds = timeSeconds,
    });
    return frame;
}

AnimationController makeController(float fadeSeconds = 0.1f)
{
    AnimationController controller(fadeSeconds);
    controller.setClip(RenderAnimation::RogueIdle, makeClip("idle"));
    controller.setClip(RenderAnimation::RogueMovement, makeClip("movement"));
    controller.setClip(RenderAnimation::RoguePush, makeClip("push"));
    return controller;
}

void testSelectsFirstAnimatedRogueAndDeduplicates()
{
    TEST("selectsFirstAnimatedRogueAndDeduplicates");
    AnimationController controller = makeController();
    CHECK(!controller.update({}));

    RenderFrameData frame;
    frame.tiles.push_back({
        .model = RenderModel::Stone,
        .animation = RenderAnimation::RoguePush,
        .animationTimeSeconds = 8.0f,
    });
    frame.tiles.push_back({
        .model = RenderModel::Rogue,
        .animation = RenderAnimation::RogueIdle,
        .animationTimeSeconds = 1.0f,
    });
    frame.tiles.push_back({
        .model = RenderModel::Rogue,
        .animation = RenderAnimation::RoguePush,
        .animationTimeSeconds = 2.0f,
    });

    const auto request = controller.update(frame);
    CHECK(request.has_value());
    CHECK(!request->blended());
    CHECK(request->toClip->name == "idle");
    CHECK(near(request->toTimeSeconds, 1.0f));
    CHECK(!controller.update(frame));

    frame.tiles[1].animationTimeSeconds = 1.25f;
    const auto advanced = controller.update(frame);
    CHECK(advanced.has_value());
    CHECK(!advanced->blended());
    CHECK(near(advanced->toTimeSeconds, 1.25f));
}

void testCrossfadeProgressesAndCompletes()
{
    TEST("crossfadeProgressesAndCompletes");
    AnimationController controller = makeController(0.1f);
    CHECK(controller.update(frameWithAnimation(RenderAnimation::RogueIdle, 1.0f)).has_value());

    const auto halfway = controller.update(frameWithAnimation(RenderAnimation::RogueMovement, 1.05f));
    CHECK(halfway.has_value());
    CHECK(halfway->blended());
    CHECK(halfway->fromClip->name == "idle");
    CHECK(halfway->toClip->name == "movement");
    CHECK(near(halfway->fromTimeSeconds, 1.05f));
    CHECK(near(halfway->blend, 0.5f));

    const auto complete = controller.update(frameWithAnimation(RenderAnimation::RogueMovement, 1.1f));
    CHECK(complete.has_value());
    CHECK(!complete->blended());
    CHECK(complete->toClip->name == "movement");
}

void testReverseTimeStillAdvancesFade()
{
    TEST("reverseTimeStillAdvancesFade");
    AnimationController controller = makeController(0.1f);
    CHECK(controller.update(frameWithAnimation(RenderAnimation::RogueIdle, 1.0f)).has_value());

    const auto request = controller.update(frameWithAnimation(RenderAnimation::RoguePush, 0.95f));
    CHECK(request.has_value());
    CHECK(request->blended());
    CHECK(request->fromClip->name == "idle");
    CHECK(near(request->fromTimeSeconds, 0.95f));
    CHECK(near(request->blend, 0.5f));
}

void testPreviewOverridesAndThenReleasesGameplay()
{
    TEST("previewOverridesAndThenReleasesGameplay");
    AnimationController controller = makeController();
    CHECK(controller.update(frameWithAnimation(RenderAnimation::RogueIdle, 0.0f)).has_value());

    GltfAnimationClip preview = makeClip("preview");
    controller.setPreview(&preview, 2.0f);
    const auto previewRequest = controller.update(frameWithAnimation(RenderAnimation::RoguePush, 4.0f));
    CHECK(previewRequest.has_value());
    CHECK(!previewRequest->blended());
    CHECK(previewRequest->toClip == &preview);
    CHECK(near(previewRequest->toTimeSeconds, 2.0f));
    CHECK(!controller.update(frameWithAnimation(RenderAnimation::RoguePush, 4.0f)));

    controller.setPreview(&preview, 2.25f);
    CHECK(controller.update(frameWithAnimation(RenderAnimation::RoguePush, 4.0f)).has_value());
    controller.setPreview(nullptr, 0.0f);
    const auto gameplayRequest = controller.update(frameWithAnimation(RenderAnimation::RoguePush, 4.0f));
    CHECK(gameplayRequest.has_value());
    CHECK(!gameplayRequest->blended());
    CHECK(gameplayRequest->toClip->name == "push");
}

void testClipValidationAndClear()
{
    TEST("clipValidationAndClear");
    AnimationController controller = makeController();
    CHECK(controller.hasClip(RenderAnimation::RogueIdle));
    CHECK(!controller.hasClip(RenderAnimation::None));

    bool threw = false;
    try {
        controller.setClip(RenderAnimation::None, makeClip("invalid"));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    controller.clear();
    CHECK(!controller.hasClip(RenderAnimation::RogueIdle));
    CHECK(!controller.update(frameWithAnimation(RenderAnimation::RogueIdle, 1.0f)));
}

} // namespace

int main()
{
    testSelectsFirstAnimatedRogueAndDeduplicates();
    testCrossfadeProgressesAndCompletes();
    testReverseTimeStillAdvancesFade();
    testPreviewOverridesAndThenReleasesGameplay();
    testClipValidationAndClear();

    if (failures == 0) {
        std::cout << "AnimationControllerTests: " << checks << " checks passed\n";
        return 0;
    }

    std::cerr << "AnimationControllerTests: " << failures << " of " << checks << " checks failed\n";
    return 1;
}
