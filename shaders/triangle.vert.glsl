#version 460

layout(location = 0) out vec4 outShadowPosition;
layout(location = 1) out float outFaceCoordU;
layout(location = 2) out float outFaceCoordV;
layout(location = 3) out vec3 outNormal;
layout(location = 4) flat out uint outTextureIndex;
layout(location = 5) flat out uint outMaterialFlags;
layout(location = 6) out vec3 outWorldPosition;
// Which draw this vertex belongs to. The fragment stage reads the same
// entry; flat because it is constant across the primitive.
layout(location = 7) flat out uint outDrawInstance;

struct DrawInstance
{
    vec4 vertices[4];
    vec4 passData[4];
    vec4 color;
    vec4 normalAndAmbientRed;
    vec4 sunDirectionAndAmbientGreen;
    vec4 sunRadianceAndAmbientBlue;
    vec4 shadowOptions;
    vec4 materialOptions;
    vec4 gridColor;
    vec4 textureOptions;
};
layout(std430, set = 0, binding = 10) readonly buffer DrawInstances
{
    DrawInstance instances[];
} drawInstances;

#define draw drawInstances.instances[gl_InstanceIndex]


struct PointLightData
{
    vec4 positionAndRange;
    vec4 colorAndIntensity;
    vec4 shadowOptions;
};
layout(std140, set = 0, binding = 7) uniform SceneFrame
{
    mat4 clipFromWorld;
    mat4 shadowFromWorld;
    vec4 cameraPositionAndNearPlane;
    PointLightData pointLights[8];
    vec4 pointLightMeta;
} frame;
const int indices[6] = int[6](0, 1, 2, 0, 2, 3);
const vec2 faceCoords[4] = vec2[4](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0),
    vec2(0.0, 1.0));

// The sun transform is a matrix now, and a matrix cannot clamp. Its CPU
// ancestor, projectShadowPoint, clamped depth into [0, 1]; without that,
// geometry outside the sun's depth range samples past the edge of the shadow
// map instead of at it.
vec4 sunShadowFromWorld(vec3 worldPosition)
{
    vec4 shadow = frame.shadowFromWorld * vec4(worldPosition, 1.0);
    shadow.z = clamp(shadow.z, 0.0, 1.0);
    return shadow;
}

void main()
{
    const int index = indices[gl_VertexIndex];
    outDrawInstance = uint(gl_InstanceIndex);
    vec4 corner = draw.vertices[index];

    // Scene corners arrive in world space and the camera is applied here. The
    // CPU used to hand this shader clip space and there was nothing left to
    // transform, which is why the world position below had to be guessed at
    // by inverting the sun's frustum.
    //
    // The UI, the top-down 2D board and its grid overlay share this shader
    // but are authored directly in clip space - they have no world position -
    // and mark themselves with w = 0. Projecting them anyway puts the entire
    // interface somewhere off screen, which looks like a window that never
    // finished loading rather than like a transform bug.
    bool worldSpace = corner.w > 0.5;
    vec3 worldPosition = corner.xyz;
    gl_Position = worldSpace
        ? frame.clipFromWorld * vec4(worldPosition, 1.0)
        : vec4(worldPosition, 1.0);
    outWorldPosition = worldPosition;
    // Zero for the clip-space callers, which is exactly what they used to
    // push: they are drawn unlit, so nothing samples this.
    outShadowPosition = worldSpace
        ? sunShadowFromWorld(worldPosition)
        : vec4(0.0);
    vec2 faceCoord = faceCoords[index] * draw.materialOptions.yz;
    outFaceCoordU = faceCoord.x;
    outFaceCoordV = faceCoord.y;
    outNormal = draw.normalAndAmbientRed.xyz;
    // Tile faces never use per-vertex texture indices (textureOptions.x is 0
    // on this path), but the shared fragment shader consumes location 4, so
    // the interface must still provide it.
    outTextureIndex = 0u;
    outMaterialFlags = 0u;
}
