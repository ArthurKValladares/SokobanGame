#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in uint inTextureIndex;
layout(location = 4) in uint inMaterialFlags;
layout(location = 8) in vec4 inTangent;
layout(location = 9) in vec2 inUv1;

layout(location = 0) out vec4 outShadowPosition;
layout(location = 1) out float outFaceCoordU;
layout(location = 2) out float outFaceCoordV;
layout(location = 3) out vec3 outNormal;
layout(location = 4) flat out uint outTextureIndex;
layout(location = 5) flat out uint outMaterialFlags;
layout(location = 6) out vec3 outWorldPosition;
layout(location = 7) flat out uint outDrawInstance;
// xyz is the tangent in the same frame as outNormal; w is the bitangent's
// handedness. The fragment stage re-orthogonalizes against the interpolated
// normal, which is what absorbs the scale this rotation deliberately skips.
layout(location = 8) out vec4 outTangent;
layout(location = 9) out vec2 outUv1;

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

// The Euler triple in normalAndAmbientRed.xyz, applied the same way to
// whatever needs it. worldFromModel would do this in one multiply and is
// right there in draw.vertices; replacing the Euler path is F1's unfinished
// business, and doing it here would move every model's shading at the same
// time as F3 changes it for other reasons.
vec3 rotateByEuler(vec3 value, vec3 rotation)
{
    float cosine = cos(rotation.x);
    float sine = sin(rotation.x);
    value = vec3(
        value.x,
        cosine * value.y - sine * value.z,
        sine * value.y + cosine * value.z);
    cosine = cos(rotation.y);
    sine = sin(rotation.y);
    value = vec3(
        cosine * value.x + sine * value.z,
        value.y,
        -sine * value.x + cosine * value.z);
    cosine = cos(rotation.z);
    sine = sin(rotation.z);
    return vec3(
        cosine * value.x - sine * value.y,
        sine * value.x + cosine * value.y,
        value.z);
}

void main()
{
    outDrawInstance = uint(gl_InstanceIndex);
    mat4 worldTransform = mat4(
        draw.vertices[0],
        draw.vertices[1],
        draw.vertices[2],
        draw.vertices[3]);

    vec3 worldPosition = (worldTransform * vec4(inPosition, 1.0)).xyz;
    gl_Position = frame.clipFromWorld * vec4(worldPosition, 1.0);
    outWorldPosition = worldPosition;
    outShadowPosition = sunShadowFromWorld(worldPosition);
    outFaceCoordU = inUv.x;
    outFaceCoordV = inUv.y;
    outTextureIndex = inTextureIndex;
    outMaterialFlags = inMaterialFlags;
    outUv1 = inUv1;
    vec3 normal = inNormal;
    // Standard models use gridColor.xyz for inverse scale and a negative W
    // as the marker. Mirror-energy models need gridColor for their effect and
    // retain unit normal scaling.
    if (draw.gridColor.w < 0.0) {
        normal *= draw.gridColor.xyz;
    }

    vec3 rotation = draw.normalAndAmbientRed.xyz;
    float cosine = cos(rotation.x);
    float sine = sin(rotation.x);
    normal = vec3(
        normal.x,
        cosine * normal.y - sine * normal.z,
        sine * normal.y + cosine * normal.z);
    cosine = cos(rotation.y);
    sine = sin(rotation.y);
    normal = vec3(
        cosine * normal.x + sine * normal.z,
        normal.y,
        -sine * normal.x + cosine * normal.z);
    cosine = cos(rotation.z);
    sine = sin(rotation.z);
    outNormal = normalize(vec3(
        cosine * normal.x - sine * normal.y,
        sine * normal.x + cosine * normal.y,
        normal.z));

    // The same Euler rotation the normal just took, and deliberately not the
    // inverse scale above it: a normal transforms by the inverse transpose, a
    // tangent by the matrix itself. For a uniform scale the two agree up to
    // length, and the fragment stage normalizes anyway.
    outTangent = vec4(rotateByEuler(inTangent.xyz, rotation), inTangent.w);
}
