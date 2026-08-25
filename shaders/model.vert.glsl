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

struct PointLightData
{
    vec4 positionAndRange;
    vec4 colorAndIntensity;
    vec4 shadowOptions;
};
layout(std140, set = 0, binding = 7) uniform SceneLighting
{
    vec4 sunShadowRightAndHalfWidth;
    vec4 sunShadowUpAndHalfHeight;
    vec4 sunShadowForwardAndDepthRange;
    vec4 sunShadowCenterAndNearestDepth;
    PointLightData pointLights[8];
    vec4 pointLightMeta;
} lighting;

struct ModelInstance
{
    vec4 clipFromModel[4];
    vec4 shadowFromModel[4];
    vec4 rotationRadians;
};
layout(std430, set = 0, binding = 10) readonly buffer ModelInstances
{
    ModelInstance instances[];
} modelInstances;

layout(push_constant) uniform PushConstants
{
    vec4 clipFromModel[4];
    vec4 shadowFromModel[4];
    vec4 color;
    vec4 normalAndAmbientRed;
    vec4 sunDirectionAndAmbientGreen;
    vec4 sunRadianceAndAmbientBlue;
    vec4 shadowOptions;
    vec4 materialOptions;
    vec4 gridColor;
    vec4 textureOptions;
} pc;

vec3 worldFromSunShadow(vec4 shadowPosition)
{
    vec3 clip = shadowPosition.xyz / max(abs(shadowPosition.w), 0.0001);
    vec3 center = lighting.sunShadowCenterAndNearestDepth.xyz;
    vec3 forward = lighting.sunShadowForwardAndDepthRange.xyz;
    float worldForward = lighting.sunShadowCenterAndNearestDepth.w +
        clip.z * lighting.sunShadowForwardAndDepthRange.w;
    float relativeForward = worldForward - dot(center, forward);
    return center +
        lighting.sunShadowRightAndHalfWidth.xyz *
            (clip.x * lighting.sunShadowRightAndHalfWidth.w) +
        lighting.sunShadowUpAndHalfHeight.xyz *
            (clip.y * lighting.sunShadowUpAndHalfHeight.w) +
        forward * relativeForward;
}

void main()
{
    ModelInstance instance = modelInstances.instances[gl_InstanceIndex];
    mat4 clipTransform = mat4(
        instance.clipFromModel[0],
        instance.clipFromModel[1],
        instance.clipFromModel[2],
        instance.clipFromModel[3]);
    mat4 shadowTransform = mat4(
        instance.shadowFromModel[0],
        instance.shadowFromModel[1],
        instance.shadowFromModel[2],
        instance.shadowFromModel[3]);

    gl_Position = clipTransform * vec4(inPosition, 1.0);
    outShadowPosition = shadowTransform * vec4(inPosition, 1.0);
    outWorldPosition = worldFromSunShadow(outShadowPosition);
    outFaceCoordU = inUv.x;
    outFaceCoordV = inUv.y;
    outTextureIndex = inTextureIndex;
    outMaterialFlags = inMaterialFlags;
    vec3 normal = inNormal;
    // Standard models use gridColor.xyz for inverse scale and a negative W
    // as the marker. Mirror-energy models need gridColor for their effect and
    // retain unit normal scaling.
    if (pc.gridColor.w < 0.0) {
        normal *= pc.gridColor.xyz;
    }

    vec3 rotation = instance.rotationRadians.xyz;
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
