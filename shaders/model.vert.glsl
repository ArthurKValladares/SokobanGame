#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in uint inTextureIndex;
layout(location = 4) in uint inMaterialFlags;

layout(location = 0) out vec4 outShadowPosition;
layout(location = 1) out float outFaceCoordU;
layout(location = 2) out float outFaceCoordV;
layout(location = 3) out vec3 outNormal;
layout(location = 4) flat out uint outTextureIndex;
layout(location = 5) flat out uint outMaterialFlags;
layout(location = 6) out vec3 outWorldPosition;
layout(location = 7) flat out uint outDrawInstance;

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
}
