#version 460
#extension GL_GOOGLE_include_directive : require
#ifndef MAX_SKIN_JOINTS
#define MAX_SKIN_JOINTS 128
#endif
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 5) in uvec4 inJoints;
layout(location = 6) in vec4 inWeights;
layout(location = 7) in uint inAttachmentNode;
layout(location = 1) out float outFaceCoordU;
layout(location = 2) out float outFaceCoordV;
layout(location = 7) flat out uint outDrawInstance;
#include "Skinning.glsl"
#include "SceneFrame.glsl"
#include "DrawInstance.glsl"
layout(push_constant) uniform PushConstants { uint drawInstance; } pc;
#define draw drawInstances.instances[pc.drawInstance]
#include "DebugOutline.glsl"
void main()
{
    vec4 position = vec4(0.0);
    vec3 normal = vec3(0.0);
    if (inAttachmentNode != 0xffffffffu) {
        mat4 matrix = skinning.instances[gl_InstanceIndex]
            .palette[MAX_SKIN_JOINTS + inAttachmentNode];
        position = matrix * vec4(inPosition, 1.0);
        normal = mat3(matrix) * inNormal;
    } else {
        for (uint i = 0; i < 4; ++i) {
            if (inWeights[i] > 0.0 && inJoints[i] < MAX_SKIN_JOINTS) {
                mat4 matrix = skinning.instances[gl_InstanceIndex].palette[inJoints[i]];
                position += matrix * vec4(inPosition, 1.0) * inWeights[i];
                normal += mat3(matrix) * inNormal * inWeights[i];
            }
        }
    }
    if (length(normal) < 0.000001) { normal = inNormal; }
    outDrawInstance = pc.drawInstance;
    outlinePosition(
        (skinning.instances[gl_InstanceIndex].modelFromSource * position).xyz,
        normalize(mat3(skinning.instances[gl_InstanceIndex].normalFromSource) * normal));
}
