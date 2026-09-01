// Headless tests for animation selection, preview, deduplication, and fades.

#include "TestHarness.hpp"

#include "engine/render/AnimationController.hpp"

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

void testHardTransitionDiscardsPreviousPose()
{
    TEST("hardTransitionDiscardsPreviousPose");
    AnimationController controller = makeController(0.1f);
    CHECK(controller.update(
        frameWithAnimation(deathClip, 0.75f)).has_value());

    RenderFrameData revived = frameWithAnimation(moveClip, 0.99f);
    revived.tiles.front().animationCrossfades = false;
    const auto request = controller.update(revived);
    CHECK(request.has_value());
    CHECK(!request->blended());
    CHECK(request->toClip->name == "movement");
    CHECK(near(request->toTimeSeconds, 0.99f));
}

void testPreviewOverridesAndThenReleasesGameplay()
{
    TEST("previewOverridesAndThenReleasesGameplay");
    AnimationController controller = makeController();
    CHECK(controller.update(frameWithAnimation(idleClip, 0.0f)).has_value());

    GltfAnimationClip preview = makeClip("preview");
    controller.setPreview(heroModel, &preview, 2.0f);
    const auto previewRequest = controller.update(frameWithAnimation(pushClip, 4.0f));
    CHECK(previewRequest.has_value());
    CHECK(!previewRequest->blended());
    CHECK(previewRequest->toClip == &preview);
    CHECK(near(previewRequest->toTimeSeconds, 2.0f));
    CHECK(!controller.update(frameWithAnimation(pushClip, 4.0f)));

    controller.setPreview(heroModel, &preview, 2.25f);
    CHECK(controller.update(frameWithAnimation(pushClip, 4.0f)).has_value());
    controller.setPreview(cubeModel, nullptr, 0.0f);
    const auto gameplayRequest = controller.update(frameWithAnimation(pushClip, 4.0f));
    CHECK(gameplayRequest.has_value());
    CHECK(!gameplayRequest->blended());
    CHECK(gameplayRequest->toClip->name == "push");

    // Instance poses back separate buffers for each frame in flight, so a
    // paused preview must still publish its pose whenever that frame recurs.
    AnimationController instanceController = makeController();
    RenderFrameData instanceFrame = frameWithAnimation(pushClip, 4.0f);
    instanceFrame.tiles.front().animationInstanceId = 17;
    instanceController.setPreview(heroModel, &preview, 2.0f);
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

    instanceController.setPreview(RenderModel { 3 }, &preview, 1.5f);
    const auto selectedModelPreview =
        instanceController.updateInstances(instanceFrame);
    CHECK(selectedModelPreview.size() == 2);
    CHECK(selectedModelPreview[0].model == heroModel);
    CHECK(selectedModelPreview[0].skinning.toClip->name == "push");
    CHECK(selectedModelPreview[1].model == RenderModel { 3 });
    CHECK(selectedModelPreview[1].skinning.toClip == &preview);
    CHECK(near(selectedModelPreview[1].skinning.toTimeSeconds, 1.5f));
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
    frame.tiles.front().animationFallbackTimeSeconds = 0.25f;
    const auto deadIdle = controller.update(frame);
    CHECK(deadIdle.has_value());
    CHECK(!deadIdle->blended());
    CHECK(deadIdle->toClip->name == "dead idle");
    CHECK(near(deadIdle->toTimeSeconds, 0.25f));
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
    float timeSeconds)
{
    // Index 3 is the first attachment vertex: the rig's own three come first.
    return skinGltfMesh(rig, clip, timeSeconds).vertices[3].position;
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
    testSelectsFirstAnimatedRogueAndDeduplicates();
    testCrossfadeProgressesAndCompletes();
    testReverseTimeStillAdvancesFade();
    testHardTransitionDiscardsPreviousPose();
    testPreviewOverridesAndThenReleasesGameplay();
    testNonLoopingAnimationFallsBackAtClipDuration();
    testClipValidationAndClear();
    testAnimatedInstancesKeepIndependentPlayback();
    testSkinnedAttachmentsInheritAnimatedNodeTransforms();
    testAnimationInterpolationModes();
    testLoadsCubicSplineAnimationLayout();

    if (failures == 0) {
        std::cout << "AnimationControllerTests: " << checks << " checks passed\n";
        return 0;
    }

    std::cerr << "AnimationControllerTests: " << failures << " of " << checks << " checks failed\n";
    return 1;
}
