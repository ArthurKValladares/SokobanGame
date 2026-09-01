#version 460
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
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
// xyz is the tangent in the same frame as outNormal; w is the bitangent's
// handedness. The fragment stage re-orthogonalizes against the interpolated
// normal.
layout(location = 8) out vec4 outTangent;
layout(location = 9) out vec2 outUv1;
// Which material this vertex belongs to, relative to the model. F3b step two
// made this the only material identity a vertex carries.
layout(location = 10) flat out uint outMaterialIndex;

#include "SceneFrame.glsl"

#include "DrawInstance.glsl"

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
    outUv1 = inUv1;
    outMaterialIndex = inMaterialIndex;
    vec3 normal = inNormal;
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
    // A tangent transforms by the matrix itself. The fragment stage projects
    // it back onto the interpolated normal's plane before normal-map use,
    // which completes the frame after non-uniform scale and interpolation.
    outTangent = vec4(normalize(modelToWorld * inTangent.xyz), inTangent.w);
}
