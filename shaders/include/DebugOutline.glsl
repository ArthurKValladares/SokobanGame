// Shared by static and skinned inverted-hull outlines. Width stays in pixels
// as the camera, viewport and model scale change.
void outlinePosition(vec3 position, vec3 normal)
{
    mat4 world = mat4(draw.vertices[0], draw.vertices[1],
                      draw.vertices[2], draw.vertices[3]);
    vec3 worldPosition = (world * vec4(position, 1.0)).xyz;
    vec3 worldNormal = normalize(transpose(inverse(mat3(world))) * normal);
    vec4 clip = frame.clipFromWorld * vec4(worldPosition, 1.0);
    vec4 displaced = frame.clipFromWorld * vec4(worldPosition + worldNormal, 1.0);
    vec2 extent = draw.passData[0].xy;
    vec2 direction = (displaced.xy * clip.w - clip.xy * displaced.w) * extent;
    float magnitude = length(direction);
    if (magnitude > 0.00001 && clip.w > 0.0) {
        clip.xy += direction / magnitude * (2.0 * draw.passData[0].z / extent) * clip.w;
    }
    gl_Position = clip;
    outFaceCoordU = 0.0;
    outFaceCoordV = 0.0;
}
