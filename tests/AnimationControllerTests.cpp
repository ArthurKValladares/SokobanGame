// Headless tests for per-instance animation selection, preview, and fades.

#include "TestHarness.hpp"

#include "engine/render/AnimationController.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace sokoban;

class TempDirectory {
public:
    TempDirectory()
    {
        const auto id =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("sokoban-animation-loader-" + std::to_string(id));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void writeTextFile(const std::filesystem::path& path, std::string_view contents)
{
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
}

void writeFloatFile(
    const std::filesystem::path& path,
    const std::vector<float>& values)
{
    std::ofstream stream(path, std::ios::binary);
    stream.write(
        reinterpret_cast<const char*>(values.data()),
        static_cast<std::streamsize>(values.size() * sizeof(float)));
}

std::string cubicSplineGltf(std::size_t translationOutputCount = 6)
{
    return R"json({
  "asset":{"version":"2.0"},
  "buffers":[{"uri":"cubic.bin","byteLength":176}],
  "bufferViews":[
    {"buffer":0,"byteOffset":0,"byteLength":8},
    {"buffer":0,"byteOffset":8,"byteLength":72},
    {"buffer":0,"byteOffset":80,"byteLength":96}
  ],
  "accessors":[
    {"bufferView":0,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[2]},
    {"bufferView":1,"componentType":5126,"count":)json" +
        std::to_string(translationOutputCount) + R"json(,"type":"VEC3"},
    {"bufferView":2,"componentType":5126,"count":6,"type":"VEC4"}
  ],
  "nodes":[{"name":"joint"}],
  "animations":[{
    "name":"Cubic",
    "samplers":[
      {"input":0,"output":1,"interpolation":"CUBICSPLINE"},
      {"input":0,"output":2,"interpolation":"CUBICSPLINE"}
    ],
    "channels":[
      {"sampler":0,"target":{"node":0,"path":"translation"}},
      {"sampler":1,"target":{"node":0,"path":"rotation"}}
    ]
  }]
})json";
}

std::vector<float> cubicSplineBuffer()
{
    return {
        // Input times.
        0.0f, 2.0f,
        // Translation: in-tangent, value, out-tangent for each key.
        9.0f, 0.0f, 0.0f,
        1.0f, 2.0f, 3.0f,
        4.0f, 0.0f, 0.0f,
        6.0f, 0.0f, 0.0f,
        7.0f, 8.0f, 9.0f,
        10.0f, 0.0f, 0.0f,
        // Rotation. Value slots are deliberately non-unit quaternions;
        // tangent slots are distinctive free vectors that must stay unscaled.
        9.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 2.0f,
        2.0f, 0.0f, 0.0f, 0.0f,
        4.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 2.0f, 0.0f,
        8.0f, 0.0f, 0.0f, 0.0f,
    };
}

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
    frame.animationTransitionTimeSeconds = timeSeconds;
    frame.tiles.push_back({
        .model = heroModel,
        .animation = animation,
        .animationInstanceId = 17,
        .animationTimeSeconds = timeSeconds,
    });
    return frame;
}

AnimationController makeController(float fadeSeconds = 0.1f)
{
    AnimationController controller(fadeSeconds);
    controller.configure(idleClip);
    controller.setClip(idleClip, makeClip("idle"));
    controller.setClip(moveClip, makeClip("movement"));
    controller.setClip(pushClip, makeClip("push"));
    controller.setClip(deathClip, makeClip("death", 1.0f));
    controller.setClip(deadIdleClip, makeClip("dead idle", 2.0f));
    controller.setClip(attackClip, makeClip("attack", 1.0f));
    return controller;
}

void testIdentifiedInstancesEmitEachFrameAcrossModels()
{
    TEST("identifiedInstancesEmitEachFrameAcrossModels");
    AnimationController controller = makeController();
    CHECK(controller.updateInstances({}).empty());

    RenderFrameData frame;
    frame.tiles.push_back({
        .model = stoneModel,
        .animation = pushClip,
        .animationTimeSeconds = 8.0f,
    });
    frame.tiles.push_back({
        .model = heroModel,
        .animation = idleClip,
        .animationInstanceId = 17,
        .animationTimeSeconds = 1.0f,
    });
    frame.tiles.push_back({
        .model = stoneModel,
        .animation = pushClip,
        .animationInstanceId = 18,
        .animationTimeSeconds = 2.0f,
    });

    const auto request = controller.updateInstances(frame);
    CHECK(request.size() == 2);
    CHECK(request[0].instanceId == 17);
    CHECK(request[0].model == heroModel);
    CHECK(!request.front().skinning.blended());
    CHECK(request.front().skinning.toClip->name == "idle");
    CHECK(near(request.front().skinning.toTimeSeconds, 1.0f));
    CHECK(request[1].instanceId == 18);
    CHECK(request[1].model == stoneModel);
    CHECK(request[1].skinning.toClip->name == "push");
    CHECK(near(request[1].skinning.toTimeSeconds, 2.0f));
    // Each frame-in-flight mesh buffer needs its pose, including paused poses.
    const auto repeated = controller.updateInstances(frame);
    CHECK(repeated.size() == 2);
    CHECK(repeated[0].skinning.toClip == request[0].skinning.toClip);
    CHECK(near(repeated[0].skinning.toTimeSeconds, 1.0f));
    CHECK(repeated[1].skinning.toClip == request[1].skinning.toClip);
    CHECK(near(repeated[1].skinning.toTimeSeconds, 2.0f));

    frame.tiles[1].animationTimeSeconds = 1.25f;
    const auto advanced = controller.updateInstances(frame);
    CHECK(advanced.size() == 2);
    CHECK(!advanced.front().skinning.blended());
    CHECK(near(advanced.front().skinning.toTimeSeconds, 1.25f));
    CHECK(near(advanced[1].skinning.toTimeSeconds, 2.0f));
}

void testCrossfadeProgressesAndCompletes()
{
    TEST("crossfadeProgressesAndCompletes");
    AnimationController controller = makeController(0.1f);
    CHECK(controller.updateInstances(frameWithAnimation(idleClip, 1.0f)).size() == 1);

    const auto halfway = controller.updateInstances(frameWithAnimation(moveClip, 1.05f));
    CHECK(halfway.size() == 1);
    CHECK(halfway.front().skinning.blended());
    CHECK(halfway.front().skinning.fromClip->name == "idle");
    CHECK(halfway.front().skinning.toClip->name == "movement");
    // A clip switch has no meaningful local-time delta: the source holds its
    // last sampled pose on the transition frame, then advances on later
    // frames once the target clock has an in-domain delta.
    CHECK(near(halfway.front().skinning.fromTimeSeconds, 1.0f));
    CHECK(near(halfway.front().skinning.blend, 0.5f));

    const auto complete = controller.updateInstances(frameWithAnimation(moveClip, 1.1f));
    CHECK(complete.size() == 1);
    CHECK(!complete.front().skinning.blended());
    CHECK(complete.front().skinning.toClip->name == "movement");
}

void testReverseTimeStillAdvancesFade()
{
    TEST("reverseTimeStillAdvancesFade");
    AnimationController controller = makeController(0.1f);
    CHECK(controller.updateInstances(frameWithAnimation(idleClip, 1.0f)).size() == 1);

    const auto request = controller.updateInstances(frameWithAnimation(pushClip, 0.95f));
    CHECK(request.size() == 1);
    CHECK(request.front().skinning.blended());
    CHECK(request.front().skinning.fromClip->name == "idle");
    CHECK(near(request.front().skinning.fromTimeSeconds, 1.0f));
    CHECK(near(request.front().skinning.blend, 0.5f));
}

void testCrossfadeClockIsIndependentFromClipLocalTime()
{
    TEST("crossfadeClockIsIndependentFromClipLocalTime");
    AnimationController controller = makeController(0.1f);

    RenderFrameData idle = frameWithAnimation(idleClip, 5.0f);
    idle.animationTransitionTimeSeconds = 10.0f;
    CHECK(controller.updateInstances(idle).size() == 1);

    // A real clip transition commonly resets local time to zero. That jump is
    // not elapsed fade time and must not instantly consume the crossfade.
    RenderFrameData move = frameWithAnimation(moveClip, 0.0f);
    move.animationTransitionTimeSeconds = 10.01f;
    const auto started = controller.updateInstances(move);
    CHECK(started.size() == 1);
    CHECK(started.front().skinning.blended());
    CHECK(near(started.front().skinning.fromTimeSeconds, 5.0f));
    CHECK(near(started.front().skinning.toTimeSeconds, 0.0f));
    CHECK(near(started.front().skinning.blend, 0.028f));

    // Transition time keeps advancing even when a target clip is paused.
    move.animationTransitionTimeSeconds = 10.05f;
    const auto halfway = controller.updateInstances(move);
    CHECK(halfway.size() == 1);
    CHECK(halfway.front().skinning.blended());
    CHECK(near(halfway.front().skinning.fromTimeSeconds, 5.0f));
    CHECK(near(halfway.front().skinning.blend, 0.5f));
}

void testHardTransitionDiscardsPreviousPose()
{
    TEST("hardTransitionDiscardsPreviousPose");
    AnimationController controller = makeController(0.1f);
    CHECK(controller.updateInstances(
        frameWithAnimation(deathClip, 0.75f)).size() == 1);

    RenderFrameData revived = frameWithAnimation(moveClip, 0.99f);
    revived.tiles.front().animationCrossfades = false;
    const auto request = controller.updateInstances(revived);
    CHECK(request.size() == 1);
    CHECK(!request.front().skinning.blended());
    CHECK(request.front().skinning.toClip->name == "movement");
    CHECK(near(request.front().skinning.toTimeSeconds, 0.99f));
}

void testPreviewOverridesAndThenReleasesGameplay()
{
    TEST("previewOverridesAndThenReleasesGameplay");
    AnimationController controller = makeController();
    RenderFrameData frame = frameWithAnimation(pushClip, 4.0f);
    frame.tiles.push_back({
        .model = RenderModel { 3 },
        .animation = attackClip,
        .animationInstanceId = 18,
        .animationTimeSeconds = 0.5f,
    });
    CHECK(controller.updateInstances(frame).size() == 2);
    GltfAnimationClip preview = makeClip("preview");
    controller.setPreview(heroModel, &preview, 2.0f);
    for (int repeatedFrame = 0; repeatedFrame < 2; ++repeatedFrame) {
        // Paused previews publish on every frame for separate mesh buffers.
        const auto requests = controller.updateInstances(frame);
        CHECK(requests.size() == 2);
        CHECK(requests[0].instanceId == 17);
        CHECK(!requests[0].skinning.blended());
        CHECK(requests[0].skinning.toClip == &preview);
        CHECK(near(requests[0].skinning.toTimeSeconds, 2.0f));
        CHECK(requests[1].instanceId == 18);
        CHECK(requests[1].skinning.toClip->name == "attack");
    }
    controller.setPreview(heroModel, &preview, 2.25f);
    const auto advanced = controller.updateInstances(frame);
    CHECK(advanced.size() == 2);
    CHECK(near(advanced[0].skinning.toTimeSeconds, 2.25f));
    GltfAnimationClip alternate = makeClip("alternate preview");
    controller.setPreview(heroModel, &alternate, 2.25f);
    const auto changed = controller.updateInstances(frame);
    CHECK(changed.size() == 2);
    CHECK(changed[0].skinning.toClip == &alternate);

    controller.setPreview(RenderModel { 3 }, &preview, 1.5f);
    const auto selectedModelPreview = controller.updateInstances(frame);
    CHECK(selectedModelPreview.size() == 2);
    CHECK(selectedModelPreview[0].model == heroModel);
    CHECK(!selectedModelPreview[0].skinning.blended());
    CHECK(selectedModelPreview[0].skinning.toClip->name == "push");
    CHECK(selectedModelPreview[1].model == RenderModel { 3 });
    CHECK(selectedModelPreview[1].skinning.toClip == &preview);
    CHECK(near(selectedModelPreview[1].skinning.toTimeSeconds, 1.5f));
    controller.setPreview(cubeModel, nullptr, 0.0f);
    const auto gameplay = controller.updateInstances(frame);
    CHECK(gameplay.size() == 2);
    CHECK(!gameplay[0].skinning.blended());
    CHECK(gameplay[0].skinning.toClip->name == "push");
    CHECK(!gameplay[1].skinning.blended());
    CHECK(gameplay[1].skinning.toClip->name == "attack");
}

void testNonLoopingAnimationFallsBackAtClipDuration()
{
    TEST("nonLoopingAnimationFallsBackAtClipDuration");
    AnimationController controller = makeController(0.1f);
    RenderFrameData frame = frameWithAnimation(deathClip, 0.99f);
    frame.tiles.front().animationFallback = deadIdleClip;
    frame.tiles.front().animationLoops = false;

    const auto death = controller.updateInstances(frame);
    CHECK(death.size() == 1);
    CHECK(death.front().skinning.toClip->name == "death");
    CHECK(death.front().skinning.toPlaybackMode == AnimationPlaybackMode::Clamp);

    frame.tiles.front().animationTimeSeconds = 1.0f;
    frame.tiles.front().animationFallbackTimeSeconds = 0.25f;
    const auto deadIdle = controller.updateInstances(frame);
    CHECK(deadIdle.size() == 1);
    CHECK(!deadIdle.front().skinning.blended());
    CHECK(deadIdle.front().skinning.toClip->name == "dead idle");
    CHECK(near(deadIdle.front().skinning.toTimeSeconds, 0.25f));
    CHECK(deadIdle.front().skinning.toPlaybackMode == AnimationPlaybackMode::Loop);
}

void testNonLoopingPlaybackModeSurvivesCrossfade()
{
    TEST("nonLoopingPlaybackModeSurvivesCrossfade");
    AnimationController controller = makeController(0.1f);

    RenderFrameData death = frameWithAnimation(deathClip, 0.75f);
    death.tiles.front().animationLoops = false;
    const auto oneShot = controller.updateInstances(death);
    CHECK(oneShot.size() == 1);
    CHECK(oneShot.front().skinning.toPlaybackMode == AnimationPlaybackMode::Clamp);

    RenderFrameData move = frameWithAnimation(moveClip, 0.0f);
    move.animationTransitionTimeSeconds = 0.80f;
    const auto transition = controller.updateInstances(move);
    CHECK(transition.size() == 1);
    CHECK(transition.front().skinning.blended());
    CHECK(transition.front().skinning.fromClip->name == "death");
    CHECK(transition.front().skinning.fromPlaybackMode == AnimationPlaybackMode::Clamp);
    CHECK(transition.front().skinning.toPlaybackMode == AnimationPlaybackMode::Loop);
}

void testClipValidationAndClear()
{
    TEST("clipValidationAndClear");
    AnimationController controller = makeController();
    CHECK(controller.hasClip(idleClip));
    CHECK(!controller.hasClip(noAnimation));
    CHECK(controller.clip(RenderAnimation { 99 }).name == "idle");
    CHECK(controller.clip(noAnimation).name == "idle");
    CHECK(controller.updateInstances(frameWithAnimation(RenderAnimation { 99 }, 0.0f)).empty());
    CHECK(controller.updateInstances(frameWithAnimation(noAnimation, 0.0f)).size() == 1);

    bool threw = false;
    try {
        controller.setClip(noAnimation, makeClip("invalid"));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    CHECK(controller.updateInstances(frameWithAnimation(pushClip, 1.0f)).size() == 1);
    GltfAnimationClip preview = makeClip("preview before clear");
    controller.setPreview(heroModel, &preview, 2.0f);
    // Leave the preview pending so the instance still has active push
    // playback. Clear must discard both, independently.
    controller.clear();
    CHECK(!controller.hasClip(idleClip));
    CHECK(controller.updateInstances(frameWithAnimation(idleClip, 1.0f)).empty());
    controller.setClip(idleClip, makeClip("reloaded idle"));
    controller.setClip(pushClip, makeClip("reloaded push"));
    const auto reloaded = controller.updateInstances(frameWithAnimation(noAnimation, 1.0f));
    CHECK(reloaded.size() == 1);
    CHECK(reloaded[0].instanceId == 17);
    CHECK(!reloaded[0].skinning.blended());
    CHECK(reloaded[0].skinning.toClip->name == "reloaded idle");
}

void testAnimatedInstancesKeepIndependentPlayback()
{
    TEST("animatedInstancesKeepIndependentPlayback");
    AnimationController controller = makeController(0.1f);
    RenderFrameData frame;
    frame.animationTransitionTimeSeconds = 1.0f;
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
    frame.tiles.push_back({
        .model = heroModel,
        .animation = idleClip,
        .animationInstanceId = 3,
        .animationTimeSeconds = 9.0f,
    });
    const auto initial = controller.updateInstances(frame);
    CHECK(initial.size() == 3);
    CHECK(initial[0].instanceId == 1);
    CHECK(initial[0].skinning.toClip->name == "idle");
    CHECK(initial[1].instanceId == 2);
    CHECK(initial[1].skinning.toClip->name == "attack");
    CHECK(initial[2].instanceId == 3);
    CHECK(near(initial[2].skinning.toTimeSeconds, 9.0f));

    frame.animationTransitionTimeSeconds = 1.05f;
    frame.tiles[0].animation = moveClip;
    frame.tiles[0].animationTimeSeconds = 1.05f;
    frame.tiles[1].animationTimeSeconds = 0.30f;
    const auto advanced = controller.updateInstances(frame);
    CHECK(advanced.size() == 3);
    CHECK(advanced[0].skinning.blended());
    CHECK(advanced[0].skinning.fromClip->name == "idle");
    CHECK(advanced[0].skinning.toClip->name == "movement");
    CHECK(near(advanced[0].skinning.blend, 0.5f));
    CHECK(!advanced[1].skinning.blended());
    CHECK(advanced[1].skinning.toClip->name == "attack");
    CHECK(!advanced[2].skinning.blended());
    CHECK(advanced[2].skinning.toClip->name == "idle");
    CHECK(near(advanced[2].skinning.toTimeSeconds, 9.0f));

    frame.tiles[1].animation = noAnimation;
    const auto fallback = controller.updateInstances(frame);
    CHECK(fallback.size() == 3);
    CHECK(fallback[1].skinning.toClip->name == "idle");

    std::reverse(frame.tiles.begin(), frame.tiles.end());
    const auto reordered = controller.updateInstances(frame);
    CHECK(reordered.size() == 3);
    CHECK(reordered[0].instanceId == 3);
    CHECK(!reordered[0].skinning.blended());
    CHECK(near(reordered[0].skinning.toTimeSeconds, 9.0f));
    CHECK(reordered[2].instanceId == 1);
    CHECK(reordered[2].skinning.fromClip->name == "idle");
    CHECK(reordered[2].skinning.toClip->name == "movement");
}

void testArenaAndVectorUpdatesProduceEquivalentInstanceRequests()
{
    TEST("arenaAndVectorUpdatesProduceEquivalentInstanceRequests");
    AnimationController vectorController = makeController();
    AnimationController arenaController = makeController();
    FrameArena arena("animation requests", 4096);
    RenderFrameData frame = frameWithAnimation(idleClip, 1.0f);
    frame.tiles.push_back({
        .model = stoneModel, .animation = deathClip,
        .animationFallback = deadIdleClip, .animationInstanceId = 18,
        .animationLoops = false, .animationTimeSeconds = 0.75f,
    });
    const auto compare = [&] {
        const auto owned = vectorController.updateInstances(frame);
        arena.reset();
        const auto live = arenaController.updateInstances(frame, arena);
        CHECK(live.arenaBacked());
        CHECK(live.droppedCount() == 0);
        CHECK(!arena.exhausted());
        CHECK(live.size() == owned.size());
        for (std::size_t index = 0; index < std::min(live.size(), owned.size()); ++index) {
            CHECK(live[index].instanceId == owned[index].instanceId);
            CHECK(live[index].model == owned[index].model);
            const auto& expected = owned[index].skinning;
            const auto& actual = live[index].skinning;
            CHECK(actual.blended() == expected.blended());
            if (actual.fromClip && expected.fromClip) {
                CHECK(actual.fromClip->name == expected.fromClip->name);
            }
            CHECK(actual.toClip->name == expected.toClip->name);
            CHECK(near(actual.fromTimeSeconds, expected.fromTimeSeconds));
            CHECK(near(actual.toTimeSeconds, expected.toTimeSeconds));
            CHECK(near(actual.blend, expected.blend));
            CHECK(actual.fromPlaybackMode == expected.fromPlaybackMode);
            CHECK(actual.toPlaybackMode == expected.toPlaybackMode);
        }
        return live.size();
    };
    CHECK(compare() == 2);
    frame.tiles[0].animation = moveClip;
    frame.tiles[0].animationTimeSeconds = 0.05f;
    frame.animationTransitionTimeSeconds = 1.05f;
    CHECK(compare() == 2); // Forward fade with a reset local clock.
    CHECK(compare() == 2); // Identical samples are still published.
    frame.tiles[0].animationTimeSeconds = 0.025f;
    frame.animationTransitionTimeSeconds = 1.025f;
    CHECK(compare() == 2); // Reverse time still advances the fade.
    frame.animationTransitionTimeSeconds = 1.05f;
    CHECK(compare() == 2); // Paused clip time, advancing transition clock.
    frame.tiles[1].animationTimeSeconds = 1.0f;
    frame.tiles[1].animationFallbackTimeSeconds = 0.25f;
    CHECK(compare() == 2); // Non-looping terminal fallback changes Clamp to Loop.

    GltfAnimationClip preview = makeClip("parity preview");
    vectorController.setPreview(heroModel, &preview, 2.0f);
    arenaController.setPreview(heroModel, &preview, 2.0f);
    CHECK(compare() == 2);
    CHECK(compare() == 2);
    vectorController.setPreview(cubeModel, nullptr, 0.0f);
    arenaController.setPreview(cubeModel, nullptr, 0.0f);
    CHECK(compare() == 2); // Exiting preview starts fresh per-instance playback.
    frame.tiles[1].animationInstanceId = 0;
    CHECK(compare() == 1); // Both overloads ignore static tiles identically.
}

void testSkinnedAttachmentsInheritAnimatedNodeTransforms()
{
    TEST("skinnedAttachmentsInheritAnimatedNodeTransforms");
    Mat4 identity {};
    identity.values[0] = 1.0f;
    identity.values[5] = 1.0f;
    identity.values[10] = 1.0f;
    identity.values[15] = 1.0f;

    SkinnedMeshData actor;
    actor.nodes = {
        SkeletonNode { .name = "root" },
        SkeletonNode { .name = "handslot.r", .parent = 0 },
    };
    actor.jointNodeIndices = { 0 };
    actor.inverseBindMatrices = { identity };
    actor.sourceMinimum = { -10.0f, -10.0f, -10.0f };
    actor.sourceMaximum = { 10.0f, 10.0f, 10.0f };
    actor.preserveSourceScale = true;
    actor.vertices.resize(3);
    for (SkinnedVertex& vertex : actor.vertices) {
        vertex.normal = { 0.0f, 1.0f, 0.0f };
        vertex.joints = { 0, 0, 0, 0 };
        vertex.weights = { 1.0f, 0.0f, 0.0f, 0.0f };
    }
    actor.indices = { 0, 1, 2 };

    MeshData axe;
    axe.vertices.resize(3);
    for (MeshVertex& vertex : axe.vertices) {
        // Source-scale static geometry is stored in engine coordinates. This
        // corresponds to glTF source position (1, 0, 0).
        vertex.position = { 1.0f, 0.0f, 0.0f };
        vertex.normal = { 0.0f, 0.0f, 1.0f };
    }
    axe.indices = { 0, 1, 2 };
    addSkinnedAttachment(actor, std::move(axe), "handslot.r");

    GltfAnimationClip clip;
    clip.durationSeconds = 1.0f;
    clip.channels = {
        AnimationChannel {
            .targetNodeName = "handslot.r",
            .path = AnimationChannelPath::Translation,
            .keyframes = {
                .times = { 0.0f, 1.0f },
                .values = {
                    Vec4 {},
                    Vec4 { 2.0f, 3.0f, 4.0f, 0.0f },
                },
            },
        },
        AnimationChannel {
            .targetNodeName = "handslot.r",
            .path = AnimationChannelPath::Rotation,
            .keyframes = {
                .times = { 0.0f, 1.0f },
                .values = {
                    Vec4 { 0.0f, 0.0f, 0.0f, 1.0f },
                    Vec4 { 0.0f, 0.0f, 1.0f, 0.0f },
                },
            },
        },
    };

    const MeshData posed = skinGltfMesh(actor, clip, 0.5f);
    CHECK(posed.vertices.size() == 6);
    CHECK(posed.indices.size() == 6);
    CHECK(posed.indices[3] == 3);
    CHECK(posed.indices[5] == 5);
    CHECK(near(posed.vertices[3].position.x, 1.0f));
    CHECK(near(posed.vertices[3].position.y, -2.0f));
    CHECK(near(posed.vertices[3].position.z, 2.5f));

    bool missingNodeThrew = false;
    try {
        MeshData missingNodeAttachment;
        missingNodeAttachment.vertices.resize(1);
        missingNodeAttachment.indices = { 0 };
        addSkinnedAttachment(
            actor, std::move(missingNodeAttachment), "missing");
    } catch (const std::runtime_error&) {
        missingNodeThrew = true;
    }
    CHECK(missingNodeThrew);
}

// A rig whose only job is to report where one attached vertex ended up, so
// that a sampling rule can be stated as "this is where linear would have put
// it" instead of as a coordinate nobody can check by eye.
SkinnedMeshData interpolationRig()
{
    Mat4 identity {};
    identity.values[0] = 1.0f;
    identity.values[5] = 1.0f;
    identity.values[10] = 1.0f;
    identity.values[15] = 1.0f;

    SkinnedMeshData rig;
    rig.nodes = {
        SkeletonNode { .name = "root" },
        SkeletonNode { .name = "handslot.r", .parent = 0 },
    };
    rig.jointNodeIndices = { 0 };
    rig.inverseBindMatrices = { identity };
    rig.sourceMinimum = { -10.0f, -10.0f, -10.0f };
    rig.sourceMaximum = { 10.0f, 10.0f, 10.0f };
    rig.preserveSourceScale = true;
    rig.vertices.resize(3);
    for (SkinnedVertex& vertex : rig.vertices) {
        vertex.normal = { 0.0f, 1.0f, 0.0f };
        vertex.joints = { 0, 0, 0, 0 };
        vertex.weights = { 1.0f, 0.0f, 0.0f, 0.0f };
    }
    rig.indices = { 0, 1, 2 };

    MeshData marker;
    marker.vertices.resize(3);
    for (MeshVertex& vertex : marker.vertices) {
        vertex.normal = { 0.0f, 0.0f, 1.0f };
    }
    marker.indices = { 0, 1, 2 };
    addSkinnedAttachment(rig, std::move(marker), "handslot.r");
    return rig;
}

// durationSeconds is separate from the last key time on purpose. Sampling
// wraps on the duration, so a clip that ends exactly on its last key loops
// back to the first rather than holding it, and the hold either side of a
// curve is unreachable unless the clip outlasts its keys.
GltfAnimationClip translationClip(
    AnimationInterpolation interpolation,
    std::vector<float> times,
    std::vector<Vec4> values,
    float durationSeconds = 0.0f)
{
    GltfAnimationClip clip;
    clip.durationSeconds =
        durationSeconds > 0.0f ? durationSeconds : times.back();
    clip.channels = {
        AnimationChannel {
            .targetNodeName = "handslot.r",
            .path = AnimationChannelPath::Translation,
            .keyframes = {
                .times = std::move(times),
                .values = std::move(values),
                .interpolation = interpolation,
            },
        },
    };
    return clip;
}

Vec3 markerPosition(
    const SkinnedMeshData& rig,
    const GltfAnimationClip& clip,
    float timeSeconds,
    AnimationPlaybackMode playbackMode = AnimationPlaybackMode::Loop)
{
    // Index 3 is the first attachment vertex: the rig's own three come first.
    return skinGltfMesh(rig, clip, timeSeconds, playbackMode)
        .vertices[3]
        .position;
}

bool samePosition(Vec3 left, Vec3 right)
{
    return near(left.x, right.x) && near(left.y, right.y) &&
        near(left.z, right.z);
}

// No asset in the project uses STEP or CUBICSPLINE - every sampler in the
// manifest's reach is LINEAR - so nothing about these two paths is covered by
// loading real content. They are covered here or they are not covered.
void testAnimationInterpolationModes()
{
    TEST("animationInterpolationModes");
    const SkinnedMeshData rig = interpolationRig();
    const std::vector<Vec4> endpoints {
        Vec4 {},
        Vec4 { 4.0f, 0.0f, 0.0f, 0.0f },
    };
    const GltfAnimationClip linear = translationClip(
        AnimationInterpolation::Linear, { 0.0f, 1.0f }, endpoints);

    // Step holds the left key until the next one arrives, so a quarter of the
    // way in it reads exactly what the start of the clip reads. Until the
    // loader read glTF's interpolation field this behaved as linear, and
    // nothing said so.
    const GltfAnimationClip step = translationClip(
        AnimationInterpolation::Step, { 0.0f, 1.0f }, endpoints);
    CHECK(samePosition(
        markerPosition(rig, step, 0.25f),
        markerPosition(rig, linear, 0.0f)));
    CHECK(!samePosition(
        markerPosition(rig, step, 0.25f),
        markerPosition(rig, linear, 0.25f)));

    // Cubic spline with flat tangents is a Hermite ease rather than a
    // straight line: a quarter of the way along it has covered 0.15625 of the
    // distance. The layout is glTF's - in-tangent, value, out-tangent - so
    // this also pins the indexing.
    // The first key's in-tangent is deliberately nothing like its value: it
    // sits immediately before the value in the same array, and reading one
    // for the other is the mistake this layout invites.
    const std::vector<Vec4> cubicValues {
        Vec4 { 9.0f, 0.0f, 0.0f, 0.0f }, Vec4 {}, Vec4 {},
        Vec4 {}, Vec4 { 4.0f, 0.0f, 0.0f, 0.0f }, Vec4 {},
    };
    const GltfAnimationClip cubic = translationClip(
        AnimationInterpolation::CubicSpline, { 0.0f, 1.0f }, cubicValues);
    CHECK(samePosition(
        markerPosition(rig, cubic, 0.25f),
        markerPosition(rig, linear, 0.15625f)));
    CHECK(samePosition(
        markerPosition(rig, cubic, 0.0f),
        markerPosition(rig, linear, 0.0f)));

    // Past the last key the curve holds its final value. Both clips run to
    // three seconds so that two seconds is genuinely past the end rather than
    // wrapped back to the start.
    const GltfAnimationClip heldLinear = translationClip(
        AnimationInterpolation::Linear, { 0.0f, 1.0f }, endpoints, 3.0f);
    const GltfAnimationClip heldCubic = translationClip(
        AnimationInterpolation::CubicSpline, { 0.0f, 1.0f }, cubicValues,
        3.0f);
    CHECK(samePosition(
        markerPosition(rig, heldCubic, 2.0f),
        markerPosition(rig, heldLinear, 2.0f)));
    CHECK(!samePosition(
        markerPosition(rig, heldCubic, 2.0f),
        markerPosition(rig, heldLinear, 0.0f)));

    // The tangent on the far side of a segment is the in-tangent of the key
    // being arrived at, not that key's out-tangent. Both are non-zero here
    // and they differ, so reading the wrong one moves the marker. Both values
    // are zero, which leaves the tangents as the only thing in play.
    const auto atTranslation = [&](float x) {
        return markerPosition(
            rig,
            translationClip(
                AnimationInterpolation::Linear,
                { 0.0f, 1.0f },
                {
                    Vec4 { x, 0.0f, 0.0f, 0.0f },
                    Vec4 { x, 0.0f, 0.0f, 0.0f },
                }),
            0.5f);
    };
    const GltfAnimationClip arriving = translationClip(
        AnimationInterpolation::CubicSpline,
        { 0.0f, 1.0f },
        {
            Vec4 { 9.0f, 0.0f, 0.0f, 0.0f }, Vec4 {}, Vec4 {},
            Vec4 { 6.0f, 0.0f, 0.0f, 0.0f }, Vec4 {},
            Vec4 { 7.0f, 0.0f, 0.0f, 0.0f },
        });
    CHECK(samePosition(
        markerPosition(rig, arriving, 0.5f), atTranslation(-0.75f)));

    // Tangents are per second, so a two-second segment scales them by two.
    // Both keys sit at the origin here: the entire excursion is the
    // out-tangent, which makes the scaling the only thing under test.
    const GltfAnimationClip tangents = translationClip(
        AnimationInterpolation::CubicSpline,
        { 0.0f, 2.0f },
        {
            Vec4 {}, Vec4 {}, Vec4 { 1.0f, 0.0f, 0.0f, 0.0f },
            Vec4 {}, Vec4 {}, Vec4 {},
        });
    CHECK(samePosition(
        markerPosition(rig, tangents, 1.0f),
        markerPosition(rig, linear, 0.0625f)));
}

void testNonLoopingSamplingHoldsTheTerminalPose()
{
    TEST("nonLoopingSamplingHoldsTheTerminalPose");
    const SkinnedMeshData rig = interpolationRig();
    const std::vector<Vec4> endpoints {
        Vec4 {},
        Vec4 { 4.0f, 0.0f, 0.0f, 0.0f },
    };
    const GltfAnimationClip oneSecond = translationClip(
        AnimationInterpolation::Linear, { 0.0f, 1.0f }, endpoints);
    const GltfAnimationClip heldReference = translationClip(
        AnimationInterpolation::Linear,
        { 0.0f, 1.0f },
        endpoints,
        2.0f);

    // Looping retains the historical modulo behavior.
    CHECK(samePosition(
        markerPosition(rig, oneSecond, 1.25f),
        markerPosition(rig, heldReference, 0.25f)));
    // A one-shot samples its last authored pose after the end instead of
    // wrapping to the first quarter of the clip and visibly replaying.
    CHECK(samePosition(
        markerPosition(
            rig,
            oneSecond,
            1.25f,
            AnimationPlaybackMode::Clamp),
        markerPosition(rig, heldReference, 1.25f)));
    CHECK(!samePosition(
        markerPosition(
            rig,
            oneSecond,
            1.25f,
            AnimationPlaybackMode::Clamp),
        markerPosition(rig, oneSecond, 1.25f)));
}

void testLoadsCubicSplineAnimationLayout()
{
    TEST("loadsCubicSplineAnimationLayout");
    TempDirectory temp;
    const std::filesystem::path gltfPath = temp.path() / "cubic.gltf";
    writeTextFile(gltfPath, cubicSplineGltf());
    writeFloatFile(temp.path() / "cubic.bin", cubicSplineBuffer());

    const GltfAnimationClip clip = loadGltfAnimationClip(gltfPath, 0);
    CHECK(clip.name == "Cubic");
    CHECK(near(clip.durationSeconds, 2.0f));
    CHECK(clip.channels.size() == 2);

    const AnimationChannel& translation = clip.channels[0];
    CHECK(translation.targetNodeName == "joint");
    CHECK(translation.path == AnimationChannelPath::Translation);
    CHECK(translation.keyframes.interpolation ==
        AnimationInterpolation::CubicSpline);
    CHECK(translation.keyframes.times.size() == 2);
    CHECK(translation.keyframes.values.size() == 6);
    CHECK(near(translation.keyframes.times[0], 0.0f));
    CHECK(near(translation.keyframes.times[1], 2.0f));
    CHECK(near(translation.keyframes.values[0].x, 9.0f));
    CHECK(near(translation.keyframes.values[1].x, 1.0f));
    CHECK(near(translation.keyframes.values[1].y, 2.0f));
    CHECK(near(translation.keyframes.values[1].z, 3.0f));
    CHECK(near(translation.keyframes.values[2].x, 4.0f));
    CHECK(near(translation.keyframes.values[3].x, 6.0f));
    CHECK(near(translation.keyframes.values[4].x, 7.0f));
    CHECK(near(translation.keyframes.values[5].x, 10.0f));

    const AnimationChannel& rotation = clip.channels[1];
    CHECK(rotation.path == AnimationChannelPath::Rotation);
    CHECK(rotation.keyframes.interpolation ==
        AnimationInterpolation::CubicSpline);
    CHECK(rotation.keyframes.values.size() == 6);
    // Tangents are free vectors. Normalizing either one would turn these
    // distinctive magnitudes into 1 and bend the sampled curve.
    CHECK(near(rotation.keyframes.values[0].x, 9.0f));
    CHECK(near(rotation.keyframes.values[2].x, 2.0f));
    CHECK(near(rotation.keyframes.values[3].x, 4.0f));
    CHECK(near(rotation.keyframes.values[5].x, 8.0f));
    // Only the middle slot of each triple is a quaternion value.
    CHECK(near(rotation.keyframes.values[1].w, 1.0f));
    CHECK(near(rotation.keyframes.values[1].z, 0.0f));
    CHECK(near(rotation.keyframes.values[4].z, 1.0f));
    CHECK(near(rotation.keyframes.values[4].w, 0.0f));

    const std::filesystem::path malformedPath = temp.path() / "malformed.gltf";
    writeTextFile(malformedPath, cubicSplineGltf(5));
    bool malformedRejected = false;
    try {
        (void)loadGltfAnimationClip(malformedPath, 0);
    } catch (const std::runtime_error&) {
        malformedRejected = true;
    }
    CHECK(malformedRejected);
}

} // namespace

int main()
{
    testIdentifiedInstancesEmitEachFrameAcrossModels();
    testCrossfadeProgressesAndCompletes();
    testReverseTimeStillAdvancesFade();
    testCrossfadeClockIsIndependentFromClipLocalTime();
    testHardTransitionDiscardsPreviousPose();
    testPreviewOverridesAndThenReleasesGameplay();
    testNonLoopingAnimationFallsBackAtClipDuration();
    testNonLoopingPlaybackModeSurvivesCrossfade();
    testClipValidationAndClear();
    testAnimatedInstancesKeepIndependentPlayback();
    testArenaAndVectorUpdatesProduceEquivalentInstanceRequests();
    testSkinnedAttachmentsInheritAnimatedNodeTransforms();
    testAnimationInterpolationModes();
    testNonLoopingSamplingHoldsTheTerminalPose();
    testLoadsCubicSplineAnimationLayout();

    if (failures == 0) {
        std::cout << "AnimationControllerTests: " << checks << " checks passed\n";
        return 0;
    }

    std::cerr << "AnimationControllerTests: " << failures << " of " << checks << " checks failed\n";
    return 1;
}
