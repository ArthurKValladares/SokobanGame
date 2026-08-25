#pragma once

#include "engine/Math.hpp"
#include "engine/render/RenderTypes.hpp"

#include <array>
#include <cstdint>

namespace sokoban {

// Scene descriptor set bindings that hold a single image each: the shadow map,
// the resolved scene color, and the post-processing inputs. The model texture
// array is the remaining binding. Kept here so the descriptor pool sizing and
// the device sampled-image limit check agree on the count.
inline constexpr uint32_t sceneSingleImageBindings = 7;

struct PointLightUniform {
    Vec4 positionAndRange {};
    Vec4 colorAndIntensity {};
    Vec4 shadowOptions {};
};

// Everything the GPU needs to know about the frame that is not per draw: the
// camera it renders through, the sun's shadow transform, and the point lights.
// One uniform buffer, written once per frame per descriptor set.
//
// The camera lives here because before C1 there was no camera on the GPU at
// all. The CPU projected every vertex and pushed clip-space corners, so the
// only spatial frame a shader could reach was the sun's shadow frustum, which
// it inverted to guess at a world position. Vertices now arrive in world
// space and every stage can simply ask where it is.
struct SceneFrameUniform {
    Mat4 clipFromWorld {};
    // Orthographic, and the w it produces is always 1. The z must still be
    // clamped to [0, 1] by whoever uses it: projectShadowPoint clamped on the
    // CPU, and a matrix has no way to. Without the clamp, geometry outside
    // the sun's depth range samples past the edge of the shadow map instead
    // of at it.
    Mat4 shadowFromWorld {};
    // xyz is the camera in world space. Specular and any view-dependent term
    // derives its view direction from this and the fragment's world position,
    // rather than from the isometric constant that used to be compiled in.
    Vec4 cameraPositionAndNearPlane {};
    std::array<PointLightUniform, RenderFrameData::pointLightCapacity>
        pointLights {};
    Vec4 pointLightMeta {};
};

static_assert(sizeof(PointLightUniform) == 48);
static_assert(sizeof(SceneFrameUniform) == 544);

// The camera and sun transforms a frame renders through, in the form the
// uniform buffer wants them. Built by the recorder from the prepared scene's
// layouts so that VulkanSceneDescriptors never has to know what an isometric
// layout is.
struct SceneCamera {
    Mat4 clipFromWorld {};
    Mat4 shadowFromWorld {};
    Vec3 position {};
    float nearPlane = 0.0f;
};

// What the w of a corner in TilePushConstants::vertices means.
//
// Most quads are scene geometry in world space and the vertex shader applies
// the camera to them. The UI, the top-down 2D board and the grid overlay are
// authored directly in clip space - they have no world position and must not
// be projected - so they say so here. Getting this wrong is not subtle: a
// clip-space quad run through the camera lands somewhere off screen, and the
// first symptom is a window with nothing in it.
inline constexpr float worldSpaceQuad = 1.0f;
inline constexpr float clipSpaceQuad = 0.0f;

struct TilePushConstants {
    // Four **world-space** corners for a quad, or the four columns of
    // worldFromModel for a mesh. Before C1 these were clip-space corners and
    // a baked clipFromModel, which is why the vertex shaders had nothing to
    // transform and nothing to report a world position from.
    //
    // The shadow pass is the exception: it has one camera per sun and six
    // more per point light, so its pipelines keep receiving clip-space
    // corners here. Nothing samples a world position in a shadow pass.
    std::array<Vec4, 4> vertices;
    // Sixty-four bytes of per-draw space, free unless a pass claims them.
    //
    // These used to be a shadow-space copy of the corners above, pushed on
    // every scene draw - the frame's sun transform, restated once per face.
    // SceneFrameUniform::shadowFromWorld covers all of them at once now, so
    // the block is no longer full. Water is the one pass that claims the
    // slot, for its border and ripple parameters; it had already been
    // squatting here, which is what the handoff meant about passes reusing
    // slots another path "leaves free".
    std::array<Vec4, 4> passData;
    Vec4 color;
    Vec4 normalAndAmbientRed;
    Vec4 sunDirectionAndAmbientGreen;
    Vec4 sunRadianceAndAmbientBlue;
    Vec4 shadowOptions;
    Vec4 materialOptions;
    Vec4 gridColor;
    Vec4 textureOptions;
};

static_assert(sizeof(TilePushConstants) == 256);

} // namespace sokoban
