#version 460

layout(location = 0) out vec3 outColor;

layout(push_constant) uniform PushConstants
{
    vec2 origin;
    vec2 size;
    vec4 color;
} pc;

const vec2 vertices[6] = vec2[](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0),
    vec2(0.0, 0.0),
    vec2(1.0, 1.0),
    vec2(0.0, 1.0)
);

void main()
{
    vec2 position = pc.origin + vertices[gl_VertexIndex] * pc.size;
    gl_Position = vec4(position, 0.0, 1.0);
    outColor = pc.color.rgb;
}
