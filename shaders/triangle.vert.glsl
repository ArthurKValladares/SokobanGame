#version 460

layout(location = 0) out vec3 outColor;

layout(push_constant) uniform PushConstants
{
    vec2 triangleOffset;
} pc;

const vec2 positions[3] = vec2[](
    vec2(0.0, -0.55),
    vec2(0.55, 0.45),
    vec2(-0.55, 0.45)
);

const vec3 colors[3] = vec3[](
    vec3(0.95, 0.25, 0.20),
    vec3(0.20, 0.80, 0.35),
    vec3(0.25, 0.45, 1.00)
);

void main()
{
    gl_Position = vec4(positions[gl_VertexIndex] + pc.triangleOffset, 0.0, 1.0);
    outColor = colors[gl_VertexIndex];
}
