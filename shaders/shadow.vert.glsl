#version 460

struct DrawInstance
{
    vec4 vertices[4];
    vec4 passData[4];
    vec4 color;
    vec4 normalAndAmbientRed;
    vec4 sunDirectionAndAmbientGreen;
    vec4 sunRadianceAndAmbientBlue;
    vec4 shadowOptions;
    vec4 materialOptions;
    vec4 gridColor;
    vec4 textureOptions;
};
layout(std430, set = 0, binding = 10) readonly buffer DrawInstances
{
    DrawInstance instances[];
} drawInstances;
layout(push_constant) uniform PushConstants
{
    DrawInstance instance;
} pc;

const int indices[6] = int[6](0, 1, 2, 0, 2, 3);

void main()
{
    // Clip space is prepared on the CPU because a point light has six
    // cameras. Instance data lets all tile casters sharing one camera use a
    // single draw instead of one push-constant update and draw per quad.
    DrawInstance draw = pc.instance.passData[0].x > 0.5
        ? pc.instance
        : drawInstances.instances[gl_InstanceIndex];
    gl_Position = draw.vertices[indices[gl_VertexIndex]];
}
