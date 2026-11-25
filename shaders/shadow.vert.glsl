#version 460
#extension GL_GOOGLE_include_directive : require

#include "DrawInstance.glsl"
#include "SceneFrame.glsl"
layout(push_constant) uniform PushConstants
{
    DrawInstance instance;
} pc;

const int indices[6] = int[6](0, 1, 2, 0, 2, 3);

const vec3 pointShadowForward[6] = vec3[6](
    vec3(1.0, 0.0, 0.0), vec3(-1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0), vec3(0.0, -1.0, 0.0),
    vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, -1.0));
const vec3 pointShadowRight[6] = vec3[6](
    vec3(0.0, 0.0, -1.0), vec3(0.0, 0.0, 1.0),
    vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0),
    vec3(1.0, 0.0, 0.0), vec3(-1.0, 0.0, 0.0));
const vec3 pointShadowUp[6] = vec3[6](
    vec3(0.0, -1.0, 0.0), vec3(0.0, -1.0, 0.0),
    vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, -1.0),
    vec3(0.0, -1.0, 0.0), vec3(0.0, -1.0, 0.0));

vec4 projectPointShadow(
    uint lightIndex, uint cubeFace, vec3 worldPosition, float nearPlane)
{
    PointLightData light = frame.pointLights[lightIndex];
    vec3 relative = worldPosition - light.positionAndRange.xyz;
    float depth = dot(relative, pointShadowForward[cubeFace]);
    float farPlane = max(light.positionAndRange.w, nearPlane + 0.001);
    float denominator = farPlane - nearPlane;
    return vec4(
        dot(relative, pointShadowRight[cubeFace]),
        dot(relative, pointShadowUp[cubeFace]),
        farPlane / denominator * depth -
            farPlane * nearPlane / denominator,
        depth);
}

void main()
{
    const float inputMode = pc.instance.passData[0].x;
    const int corner = indices[gl_VertexIndex];
    if (inputMode > 1.5) {
        // The CPU writes each light's world-space caster set once. All six
        // cube faces reuse it; projection here replaces one push/draw pair per
        // caster with a single instanced draw per face.
        gl_Position = projectPointShadow(
            uint(pc.instance.passData[0].y + 0.5),
            uint(pc.instance.passData[0].z + 0.5),
            drawInstances.instances[gl_InstanceIndex].vertices[corner].xyz,
            pc.instance.passData[0].w);
    } else if (inputMode > 0.5) {
        gl_Position = pc.instance.vertices[corner];
    } else {
        gl_Position = drawInstances.instances[gl_InstanceIndex].vertices[corner];
    }
    // Each branch reads the one corner it needs straight from its block. A
    // local DrawInstance copy would move 256 bytes per vertex. glslang 14.0
    // also gives such a local the block's ArrayStride-decorated array type,
    // which the spirv-val in validation layers 1.4.363 rejects
    // (VUID-StandaloneSpirv-None-10684).
}
