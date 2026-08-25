#version 460

#ifndef MAX_SKIN_JOINTS
#define MAX_SKIN_JOINTS 128
#endif

layout(location = 0) in vec3 inPosition;
layout(location = 5) in uvec4 inJoints;
layout(location = 6) in vec4 inWeights;
layout(location = 7) in uint inAttachmentNode;

struct SkinningInstance { mat4 palette[MAX_SKIN_JOINTS + 128]; mat4 modelFromSource; mat4 normalFromSource; };
layout(std430, set = 0, binding = 9) readonly buffer SkinningPalette { SkinningInstance instances[]; } skinning;
layout(push_constant) uniform PushConstants { vec4 shadowFromModel[4]; vec4 passData[4]; vec4 color; vec4 normalAndAmbientRed; vec4 sunDirectionAndAmbientGreen; vec4 sunRadianceAndAmbientBlue; vec4 shadowOptions; vec4 materialOptions; vec4 gridColor; vec4 textureOptions; } pc;

// SkinningInstance is 258 mat4 (16512 bytes). Never bind one to a local:
// `SkinningInstance instance = skinning.instances[i]` is a whole-struct
// OpLoad per vertex invocation, and drivers that fail to scalarize it spill
// catastrophically. Index the members through the buffer instead.
void main() {
    vec4 sourcePosition = vec4(0.0);
    if (inAttachmentNode != 0xffffffffu) {
        sourcePosition =
            skinning.instances[gl_InstanceIndex]
                .palette[MAX_SKIN_JOINTS + inAttachmentNode] *
            vec4(inPosition, 1.0);
    } else {
        for (uint i = 0; i < 4; ++i) {
            if (inWeights[i] > 0.0 && inJoints[i] < MAX_SKIN_JOINTS) {
                sourcePosition +=
                    (skinning.instances[gl_InstanceIndex]
                            .palette[inJoints[i]] *
                        vec4(inPosition, 1.0)) *
                    inWeights[i];
            }
        }
    }
    mat4 shadowTransform = mat4(pc.shadowFromModel[0], pc.shadowFromModel[1], pc.shadowFromModel[2], pc.shadowFromModel[3]);
    gl_Position = shadowTransform *
        skinning.instances[gl_InstanceIndex].modelFromSource * sourcePosition;
}
