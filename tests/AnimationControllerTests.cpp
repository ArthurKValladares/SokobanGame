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

// Runtime ids as an asset manifest would assign them.
constexpr RenderModel heroModel { 1 };
constexpr RenderModel stoneModel { 2 };
constexpr RenderAnimation idleClip { 1 };
constexpr RenderAnimation moveClip { 2 };
constexpr RenderAnimation pushClip { 3 };
constexpr RenderAnimation deathClip { 4 };
constexpr RenderAnimation deadIdleClip { 5 };
constexpr RenderAnimation attackClip { 6 };

bool near(float left, float right)
{
    return std::abs(left - right) < 0.0001f;
}

GltfAnimationClip makeClip(std::string name, float durationSeconds = 0.0f)
{
    GltfAnimationClip clip;
    clip.name = std::move(name);
    clip.durationSeconds = durationSeconds;
    clip.channels.emplace_back();
    return clip;
}

RenderFrameData frameWithAnimation(RenderAnimation animation, float timeSeconds)
{
    RenderFrameData frame;
    frame.tiles.push_back({
        .model = heroModel,
        .animation = animation,
        .animationTimeSeconds = timeSeconds,
    });
    return frame;
}

AnimationController makeController(float fadeSeconds = 0.1f)
{
    AnimationController controller(fadeSeconds);
    controller.configure(heroModel, idleClip);
    controller.setClip(idleClip, makeClip("idle"));
    controller.setClip(moveClip, makeClip("movement"));
    controller.setClip(pushClip, makeClip("push"));
    controller.setClip(deathClip, makeClip("death", 1.0f));
    controller.setClip(deadIdleClip, makeClip("dead idle", 2.0f));
    controller.setClip(attackClip, makeClip("attack", 1.0f));
    return controller;
}

void testSelectsFirstAnimatedRogueAndDeduplicates()
{
    TEST("selectsFirstAnimatedRogueAndDeduplicates");
    AnimationController controller = makeController();
    CHECK(!controller.update({}));

    RenderFrameData frame;
    frame.tiles.push_back({
        .model = stoneModel,
        .animation = pushClip,
        .animationTimeSeconds = 8.0f,
    });
    frame.tiles.push_back({
        .model = heroModel,
        .animation = idleClip,
        .animationTimeSeconds = 1.0f,
    });
    frame.tiles.push_back({
        .model = heroModel,
        .animation = pushClip,
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
    CHECK(controller.update(frameWithAnimation(idleClip, 1.0f)).has_value());

    const auto halfway = controller.update(frameWithAnimation(moveClip, 1.05f));
    CHECK(halfway.has_value());
    CHECK(halfway->blended());
    CHECK(halfway->fromClip->name == "idle");
    CHECK(halfway->toClip->name == "movement");
    CHECK(near(halfway->fromTimeSeconds, 1.05f));
    CHECK(near(halfway->blend, 0.5f));

    const auto complete = controller.update(frameWithAnimation(moveClip, 1.1f));
    CHECK(complete.has_value());
    CHECK(!complete->blended());
    CHECK(complete->toClip->name == "movement");
}

void testReverseTimeStillAdvancesFade()
{
    TEST("reverseTimeStillAdvancesFade");
    AnimationController controller = makeController(0.1f);
    CHECK(controller.update(frameWithAnimation(idleClip, 1.0f)).has_value());

    const auto request = controller.update(frameWithAnimation(pushClip, 0.95f));
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
    CHECK(controller.update(frameWithAnimation(idleClip, 0.0f)).has_value());

    GltfAnimationClip preview = makeClip("preview");
    controller.setPreview(&preview, 2.0f);
    const auto previewRequest = controller.update(frameWithAnimation(pushClip, 4.0f));
    CHECK(previewRequest.has_value());
    CHECK(!previewRequest->blended());
    CHECK(previewRequest->toClip == &preview);
    CHECK(near(previewRequest->toTimeSeconds, 2.0f));
    CHECK(!controller.update(frameWithAnimation(pushClip, 4.0f)));

    controller.setPreview(&preview, 2.25f);
    CHECK(controller.update(frameWithAnimation(pushClip, 4.0f)).has_value());
    controller.setPreview(nullptr, 0.0f);
    const auto gameplayRequest = controller.update(frameWithAnimation(pushClip, 4.0f));
    CHECK(gameplayRequest.has_value());
    CHECK(!gameplayRequest->blended());
    CHECK(gameplayRequest->toClip->name == "push");

    // Instance poses back separate buffers for each frame in flight, so a
    // paused preview must still publish its pose whenever that frame recurs.
    AnimationController instanceController = makeController();
    RenderFrameData instanceFrame = frameWithAnimation(pushClip, 4.0f);
    instanceFrame.tiles.front().animationInstanceId = 17;
    instanceController.setPreview(&preview, 2.0f);
    CHECK(instanceController.updateInstances(instanceFrame).size() == 1);
    CHECK(instanceController.updateInstances(instanceFrame).size() == 1);

    instanceFrame.tiles.push_back({
        .model = RenderModel { 3 },
        .animation = attackClip,
        .animationInstanceId = 18,
        .animationTimeSeconds = 0.5f,
    });
    const auto mixedPreview = instanceController.updateInstances(instanceFrame);
    CHECK(mixedPreview.size() == 2);
    CHECK(mixedPreview[0].skinning.toClip == &preview);
    CHECK(mixedPreview[1].model == RenderModel { 3 });
    CHECK(mixedPreview[1].skinning.toClip->name == "attack");
}

void testNonLoopingAnimationFallsBackAtClipDuration()
{
    TEST("nonLoopingAnimationFallsBackAtClipDuration");
    AnimationController controller = makeController(0.1f);
    RenderFrameData frame = frameWithAnimation(deathClip, 0.99f);
    frame.tiles.front().animationFallback = deadIdleClip;
    frame.tiles.front().animationLoops = false;

    const auto death = controller.update(frame);
    CHECK(death.has_value());
    CHECK(death->toClip->name == "death");

    frame.tiles.front().animationTimeSeconds = 1.0f;
    const auto deadIdle = controller.update(frame);
    CHECK(deadIdle.has_value());
    CHECK(!deadIdle->blended());
    CHECK(deadIdle->toClip->name == "dead idle");
    CHECK(near(deadIdle->toTimeSeconds, 1.0f));
}

void testClipValidationAndClear()
{
    TEST("clipValidationAndClear");
    AnimationController controller = makeController();
    CHECK(controller.hasClip(idleClip));
    CHECK(!controller.hasClip(noAnimation));

    bool threw = false;
    try {
        controller.setClip(noAnimation, makeClip("invalid"));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    controller.clear();
    CHECK(!controller.hasClip(idleClip));
    CHECK(!controller.update(frameWithAnimation(idleClip, 1.0f)));
}

void testAnimatedInstancesKeepIndependentPlayback()
{
    TEST("animatedInstancesKeepIndependentPlayback");
    AnimationController controller = makeController(0.1f);
    RenderFrameData frame;
    frame.tiles.push_back({
        .model = heroModel,
        .animation = idleClip,
        .animationInstanceId = 1,
        .animationTimeSeconds = 1.0f,
    });
    frame.tiles.push_back({
        .model = RenderModel { 3 },
        .animation = attackClip,
        .animationFallback = idleClip,
        .animationInstanceId = 2,
        .animationLoops = false,
        .animationTimeSeconds = 0.25f,
    });
    const auto initial = controller.updateInstances(frame);
    CHECK(initial.size() == 2);
    CHECK(initial[0].instanceId == 1);
    CHECK(initial[0].skinning.toClip->name == "idle");
    CHECK(initial[1].instanceId == 2);
    CHECK(initial[1].skinning.toClip->name == "attack");

    frame.tiles[0].animation = moveClip;
    frame.tiles[0].animationTimeSeconds = 1.05f;
    frame.tiles[1].animationTimeSeconds = 0.30f;
    const auto advanced = controller.updateInstances(frame);
    CHECK(advanced.size() == 2);
    CHECK(advanced[0].skinning.blended());
    CHECK(advanced[0].skinning.fromClip->name == "idle");
    CHECK(advanced[0].skinning.toClip->name == "movement");
    CHECK(!advanced[1].skinning.blended());
    CHECK(advanced[1].skinning.toClip->name == "attack");

    frame.tiles[1].animation = noAnimation;
    const auto fallback = controller.updateInstances(frame);
    CHECK(fallback.size() == 2);
    CHECK(fallback[1].skinning.toClip->name == "idle");
}

} // namespace

int main()
{
    testSelectsFirstAnimatedRogueAndDeduplicates();
    testCrossfadeProgressesAndCompletes();
    testReverseTimeStillAdvancesFade();
    testPreviewOverridesAndThenReleasesGameplay();
    testNonLoopingAnimationFallsBackAtClipDuration();
    testClipValidationAndClear();
    testAnimatedInstancesKeepIndependentPlayback();

    if (failures == 0) {
        std::cout << "AnimationControllerTests: " << checks << " checks passed\n";
        return 0;
    }

    std::cerr << "AnimationControllerTests: " << failures << " of " << checks << " checks failed\n";
    return 1;
}
