#version 460

#ifndef MAX_SKIN_JOINTS
#define MAX_SKIN_JOINTS 128
#endif

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in uint inTextureIndex;
layout(location = 4) in uint inMaterialFlags;
layout(location = 5) in uvec4 inJoints;
layout(location = 6) in vec4 inWeights;
layout(location = 7) in uint inAttachmentNode;

layout(location = 0) out vec4 outShadowPosition;
layout(location = 1) out float outFaceCoordU;
layout(location = 2) out float outFaceCoordV;
layout(location = 3) out vec3 outNormal;
layout(location = 4) flat out uint outTextureIndex;
layout(location = 5) flat out uint outMaterialFlags;
layout(location = 6) out vec3 outWorldPosition;

struct SkinningInstance {
    mat4 palette[MAX_SKIN_JOINTS + 128];
    mat4 modelFromSource;
    mat4 normalFromSource;
};
layout(std430, set = 0, binding = 9) readonly buffer SkinningPalette
{
    SkinningInstance instances[];
} skinning;

struct PointLightData { vec4 positionAndRange; vec4 colorAndIntensity; vec4 shadowOptions; };
layout(std140, set = 0, binding = 7) uniform SceneLighting {
    vec4 sunShadowRightAndHalfWidth; vec4 sunShadowUpAndHalfHeight;
    vec4 sunShadowForwardAndDepthRange; vec4 sunShadowCenterAndNearestDepth;
    PointLightData pointLights[8]; vec4 pointLightMeta;
} lighting;

layout(push_constant) uniform PushConstants {
    vec4 clipFromModel[4]; vec4 shadowFromModel[4]; vec4 color;
    vec4 normalAndAmbientRed; vec4 sunDirectionAndAmbientGreen;
    vec4 sunRadianceAndAmbientBlue; vec4 shadowOptions; vec4 materialOptions;
    vec4 gridColor; vec4 textureOptions;
} pc;

vec3 worldFromSunShadow(vec4 shadowPosition) {
    vec3 clip = shadowPosition.xyz / max(abs(shadowPosition.w), 0.0001);
    vec3 center = lighting.sunShadowCenterAndNearestDepth.xyz;
    vec3 forward = lighting.sunShadowForwardAndDepthRange.xyz;
    float worldForward = lighting.sunShadowCenterAndNearestDepth.w + clip.z * lighting.sunShadowForwardAndDepthRange.w;
    return center + lighting.sunShadowRightAndHalfWidth.xyz * (clip.x * lighting.sunShadowRightAndHalfWidth.w) + lighting.sunShadowUpAndHalfHeight.xyz * (clip.y * lighting.sunShadowUpAndHalfHeight.w) + forward * (worldForward - dot(center, forward));
}

void main() {
    SkinningInstance instance = skinning.instances[gl_InstanceIndex];
    vec4 sourcePosition = vec4(0.0);
    vec3 sourceNormal = vec3(0.0);
    if (inAttachmentNode != 0xffffffffu) {
        mat4 matrix = instance.palette[MAX_SKIN_JOINTS + inAttachmentNode];
        sourcePosition = matrix * vec4(inPosition, 1.0);
        sourceNormal = mat3(matrix) * inNormal;
    } else {
        for (uint i = 0; i < 4; ++i) {
            if (inWeights[i] > 0.0 && inJoints[i] < MAX_SKIN_JOINTS) {
                mat4 matrix = instance.palette[inJoints[i]];
                sourcePosition += (matrix * vec4(inPosition, 1.0)) * inWeights[i];
                sourceNormal += (mat3(matrix) * inNormal) * inWeights[i];
            }
        }
        if (length(sourceNormal) < 0.000001) { sourceNormal = inNormal; }
    }
    vec3 position = (instance.modelFromSource * sourcePosition).xyz;
    vec3 normal = normalize(mat3(instance.normalFromSource) * normalize(sourceNormal));
    mat4 clipTransform = mat4(pc.clipFromModel[0], pc.clipFromModel[1], pc.clipFromModel[2], pc.clipFromModel[3]);
    mat4 shadowTransform = mat4(pc.shadowFromModel[0], pc.shadowFromModel[1], pc.shadowFromModel[2], pc.shadowFromModel[3]);
    gl_Position = clipTransform * vec4(position, 1.0);
    outShadowPosition = shadowTransform * vec4(position, 1.0);
    outWorldPosition = worldFromSunShadow(outShadowPosition);
    outFaceCoordU = inUv.x; outFaceCoordV = inUv.y;
    outTextureIndex = inTextureIndex; outMaterialFlags = inMaterialFlags;
    if (pc.gridColor.w < 0.0) { normal *= pc.gridColor.xyz; }
    vec3 rotation = pc.normalAndAmbientRed.xyz;
    float cosine = cos(rotation.x); float sine = sin(rotation.x);
    normal = vec3(normal.x, cosine * normal.y - sine * normal.z, sine * normal.y + cosine * normal.z);
    cosine = cos(rotation.y); sine = sin(rotation.y);
    normal = vec3(cosine * normal.x + sine * normal.z, normal.y, -sine * normal.x + cosine * normal.z);
    cosine = cos(rotation.z); sine = sin(rotation.z);
    outNormal = normalize(vec3(cosine * normal.x - sine * normal.y, sine * normal.x + cosine * normal.y, normal.z));
}
