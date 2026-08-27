#version 460

#ifndef MAX_SKIN_JOINTS
#define MAX_SKIN_JOINTS 128
#endif

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 5) in uvec4 inJoints;
layout(location = 6) in vec4 inWeights;
layout(location = 7) in uint inAttachmentNode;
layout(location = 8) in vec4 inTangent;
layout(location = 9) in vec2 inUv1;
layout(location = 10) in uint inMaterialIndex;

layout(location = 0) out vec4 outShadowPosition;
layout(location = 1) out float outFaceCoordU;
layout(location = 2) out float outFaceCoordV;
layout(location = 3) out vec3 outNormal;
// Locations 4 and 5 are retired. They carried a texture index and a material
// flag word per vertex until F3b moved both into the material buffer, where
// one entry serves every vertex sharing a material. Gaps here are normal -
// a fragment shader declares only the locations it reads, and water declares
// three of these - so they are left free rather than closed by renumbering
// every scene shader.
layout(location = 6) out vec3 outWorldPosition;
layout(location = 7) flat out uint outDrawInstance;
layout(location = 8) out vec4 outTangent;
layout(location = 9) out vec2 outUv1;
// Which material this vertex belongs to. Nothing reads it yet; F3b-2 is
// where it stops being the vertex's job to carry a texture index at all.
layout(location = 10) flat out uint outMaterialIndex;

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
    vec3 sourceTangent = vec3(0.0);
    if (inAttachmentNode != 0xffffffffu) {
        mat4 matrix = skinning.instances[gl_InstanceIndex]
                          .palette[MAX_SKIN_JOINTS + inAttachmentNode];
        sourcePosition = matrix * vec4(inPosition, 1.0);
        sourceNormal = mat3(matrix) * inNormal;
        sourceTangent = mat3(matrix) * inTangent.xyz;
    } else {
        for (uint i = 0; i < 4; ++i) {
            if (inWeights[i] > 0.0 && inJoints[i] < MAX_SKIN_JOINTS) {
                mat4 matrix = skinning.instances[gl_InstanceIndex]
                                  .palette[inJoints[i]];
                sourcePosition += (matrix * vec4(inPosition, 1.0)) * inWeights[i];
                sourceNormal += (mat3(matrix) * inNormal) * inWeights[i];
                sourceTangent += (mat3(matrix) * inTangent.xyz) * inWeights[i];
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
    outUv1 = inUv1;
    outMaterialIndex = inMaterialIndex;
    // modelFromSource, not normalFromSource: a tangent transforms by the
    // matrix, a normal by its inverse transpose.
    vec3 tangent = mat3(skinning.instances[gl_InstanceIndex].modelFromSource) *
        sourceTangent;
    // Rotation and scale are already in worldFromModel: its first three
    // columns are the model's axes, so their lengths are the scale and the
    // columns divided by that are the rotation. This used to rebuild the same
    // rotation from an Euler triple the recorder sent alongside the matrix -
    // three sines and three cosines per vertex to reconstruct something that
    // was sitting in draw.vertices the whole time - and took the inverse
    // scale from gridColor.xyz, which the mirror-energy path could not use
    // because it needed gridColor for its effect. Deriving both from the
    // matrix costs neither of those and drops the special case.
    //
    // The sign of a negative scale is lost here, exactly as it was before:
    // the recorder's inverse scale went through std::abs.
    mat3 modelToWorld = mat3(worldTransform);
    vec3 inverseScale = 1.0 / max(
        vec3(
            length(modelToWorld[0]),
            length(modelToWorld[1]),
            length(modelToWorld[2])),
        vec3(0.0001));
    mat3 rotationOnly = mat3(
        modelToWorld[0] * inverseScale.x,
        modelToWorld[1] * inverseScale.y,
        modelToWorld[2] * inverseScale.z);
    // A normal transforms by the inverse transpose, which for a rotation
    // times a diagonal scale is the rotation times the inverse scale.
    outNormal = normalize(rotationOnly * (normal * inverseScale));
    // A tangent transforms by the matrix itself. The Euler path applied the
    // rotation without the scale, which is the same answer only when the
    // scale is uniform; nothing reads the tangent yet, so this becomes right
    // before it becomes visible.
    outTangent = vec4(normalize(modelToWorld * tangent), inTangent.w);
}
