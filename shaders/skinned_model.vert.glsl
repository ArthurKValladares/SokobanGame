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
layout(location = 7) flat out uint outDrawInstance;

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
layout(std140, set = 0, binding = 7) uniform SceneFrame {
    mat4 clipFromWorld; mat4 shadowFromWorld;
    vec4 cameraPositionAndNearPlane;
    PointLightData pointLights[8]; vec4 pointLightMeta;
} frame;

struct DrawInstance {
    vec4 vertices[4]; vec4 passData[4]; vec4 color;
    vec4 normalAndAmbientRed; vec4 sunDirectionAndAmbientGreen;
    vec4 sunRadianceAndAmbientBlue; vec4 shadowOptions; vec4 materialOptions;
    vec4 gridColor; vec4 textureOptions;
};
layout(std430, set = 0, binding = 10) readonly buffer DrawInstances {
    DrawInstance instances[];
} drawInstances;

// gl_InstanceIndex is spoken for here - it indexes the skinning palette - so
// this pipeline is the one that still needs its draw-instance index pushed.
layout(push_constant) uniform PushConstants { uint drawInstance; } pc;

#define draw drawInstances.instances[pc.drawInstance]

// See model.vert: the matrix cannot clamp the way projectShadowPoint did.
vec4 sunShadowFromWorld(vec3 worldPosition) {
    vec4 shadow = frame.shadowFromWorld * vec4(worldPosition, 1.0);
    shadow.z = clamp(shadow.z, 0.0, 1.0);
    return shadow;
}

// SkinningInstance is 258 mat4 (16512 bytes). Never bind one to a local:
// `SkinningInstance instance = skinning.instances[i]` is a whole-struct
// OpLoad per vertex invocation, and drivers that fail to scalarize it spill
// catastrophically. Index the members through the buffer instead; the
// per-matrix loads below are the only reads that should reach memory.
void main() {
    vec4 sourcePosition = vec4(0.0);
    vec3 sourceNormal = vec3(0.0);
    if (inAttachmentNode != 0xffffffffu) {
        mat4 matrix = skinning.instances[gl_InstanceIndex]
                          .palette[MAX_SKIN_JOINTS + inAttachmentNode];
        sourcePosition = matrix * vec4(inPosition, 1.0);
        sourceNormal = mat3(matrix) * inNormal;
    } else {
        for (uint i = 0; i < 4; ++i) {
            if (inWeights[i] > 0.0 && inJoints[i] < MAX_SKIN_JOINTS) {
                mat4 matrix = skinning.instances[gl_InstanceIndex]
                                  .palette[inJoints[i]];
                sourcePosition += (matrix * vec4(inPosition, 1.0)) * inWeights[i];
                sourceNormal += (mat3(matrix) * inNormal) * inWeights[i];
            }
        }
        if (length(sourceNormal) < 0.000001) { sourceNormal = inNormal; }
    }
    vec3 position =
        (skinning.instances[gl_InstanceIndex].modelFromSource * sourcePosition)
            .xyz;
    vec3 normal = normalize(
        mat3(skinning.instances[gl_InstanceIndex].normalFromSource) *
        normalize(sourceNormal));
    outDrawInstance = pc.drawInstance;
    mat4 worldTransform = mat4(draw.vertices[0], draw.vertices[1], draw.vertices[2], draw.vertices[3]);
    vec3 worldPosition = (worldTransform * vec4(position, 1.0)).xyz;
    gl_Position = frame.clipFromWorld * vec4(worldPosition, 1.0);
    outWorldPosition = worldPosition;
    outShadowPosition = sunShadowFromWorld(worldPosition);
    outFaceCoordU = inUv.x; outFaceCoordV = inUv.y;
    outTextureIndex = inTextureIndex; outMaterialFlags = inMaterialFlags;
    if (draw.gridColor.w < 0.0) { normal *= draw.gridColor.xyz; }
    vec3 rotation = draw.normalAndAmbientRed.xyz;
    float cosine = cos(rotation.x); float sine = sin(rotation.x);
    normal = vec3(normal.x, cosine * normal.y - sine * normal.z, sine * normal.y + cosine * normal.z);
    cosine = cos(rotation.y); sine = sin(rotation.y);
    normal = vec3(cosine * normal.x + sine * normal.z, normal.y, -sine * normal.x + cosine * normal.z);
    cosine = cos(rotation.z); sine = sin(rotation.z);
    outNormal = normalize(vec3(cosine * normal.x - sine * normal.y, sine * normal.x + cosine * normal.y, normal.z));
}
