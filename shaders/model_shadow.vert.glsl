#version 460
#extension GL_GOOGLE_include_directive : require

#include "GroundRim.glsl"

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstants
{
    vec4 shadowFromModel[4];
    vec4 passData[4];
    vec4 color;
    vec4 normalAndAmbientRed;
    vec4 sunDirectionAndAmbientGreen;
    vec4 sunRadianceAndAmbientBlue;
    vec4 shadowOptions;
    vec4 materialOptions;
    vec4 gridColor;
    vec4 textureOptions;
} pc;

void main()
{
    mat4 shadowTransform = mat4(
        pc.shadowFromModel[0],
        pc.shadowFromModel[1],
        pc.shadowFromModel[2],
        pc.shadowFromModel[3]);
    GroundRimDeformation rim = groundRimDeformation(
        inPosition, pc.passData[2], uint(pc.passData[3].x + 0.5), pc.passData[3].yz);
    gl_Position = shadowTransform * vec4(rim.position, 1.0);
}
