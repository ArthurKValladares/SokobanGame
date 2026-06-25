#version 460

layout(location = 0) in vec3 inPosition;

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
} pc;

void main()
{
    mat4 shadowTransform = mat4(
        pc.shadowFromModel[0],
        pc.shadowFromModel[1],
        pc.shadowFromModel[2],
        pc.shadowFromModel[3]);
    gl_Position = shadowTransform * vec4(inPosition, 1.0);
}
