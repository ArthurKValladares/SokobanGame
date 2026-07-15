#version 460

// Fullscreen triangle from gl_VertexIndex; fragment shaders derive UVs from
// gl_FragCoord, so no varyings are needed.
void main()
{
    const vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2(3.0, -1.0),
        vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
