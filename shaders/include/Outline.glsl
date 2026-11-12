// Shared by gameplay and debug static/skinned inverted-hull outlines. Width stays in pixels
// as the camera, viewport and model scale change.
bool usesSurfaceContour()
{
    return draw.passData[0].w > 1.5 && draw.passData[0].w < 2.5;
}

bool usesOccludedSilhouette()
{
    return draw.passData[0].w > 3.5;
}

void outlinePosition(vec3 position, vec3 normal)
{
    mat4 world = mat4(draw.vertices[0], draw.vertices[1],
                      draw.vertices[2], draw.vertices[3]);
    vec3 worldPosition = (world * vec4(position, 1.0)).xyz;
    vec4 clip = frame.clipFromWorld * vec4(worldPosition, 1.0);
    bool faceted = draw.passData[0].w > 2.5 && !usesOccludedSilhouette();
    // A radial direction keeps coincident vertices together at hard edges.
    vec3 worldNormal = faceted
        ? worldPosition - (world * vec4(0.5, 0.5, 0.5, 1.0)).xyz
        : transpose(inverse(mat3(world))) * normal;
    if (usesSurfaceContour() || usesOccludedSilhouette()) {
        // The debug silhouette keeps the mesh's screen shape at the front
        // depth of its bounds, so the entity cannot hide parts of itself.
        if (usesOccludedSilhouette()) {
            clip.z = draw.passData[0].z * clip.w;
        }
        gl_Position = clip;
        outFaceCoordU = dot(normalize(worldNormal),
            normalize(frame.cameraPositionAndNearPlane.xyz - worldPosition));
        outFaceCoordV = 0.0;
        return;
    }
    vec4 clipNormal = frame.clipFromWorld * vec4(worldNormal, 0.0);
    vec2 extent = draw.passData[0].xy;
    vec2 direction = (clipNormal.xy * clip.w - clip.xy * clipNormal.w) * extent;
    float magnitude = length(direction);
    if (magnitude > 0.00001 && clip.w > 0.0) {
        clip.xy += direction / magnitude * (2.0 * draw.passData[0].z / extent) * clip.w;
    }
    gl_Position = clip;
    outFaceCoordU = 0.0;
    outFaceCoordV = 0.0;
}
