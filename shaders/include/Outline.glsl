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

// A hull vertex is pushed outward on the screen, which moves it in the plane
// of the screen. Under a camera that looks down, that carries the vertices
// around an object's foot below the floor it stands on, where the ground
// hides them: the outline thinned, broke up or came away from the object
// along the bottom of everything standing on a tile. A vertex that ends up
// under the floor is therefore slid back along its own line of sight until
// it rests on the floor. It stays exactly where it was on the screen and is
// no longer behind the ground.
//
// The floor is the plane of the object's own base, which is where a standing
// object meets its tile. A vertex that was below that plane before it was
// pushed belongs to a part sunk into the ground and is left there. The hull
// rests a hair above the floor so that it does not fight the ground for the
// same depth.
const float outlineFloorClearance = 0.01;
const float outlineSunkenTolerance = 0.02;
const float outlineDepthMaskBias = 0.00002;

vec4 outlineRestingOnFloor(
    vec4 clip,
    vec2 clipOffset,
    vec3 worldPosition,
    float baseHeight)
{
    if (worldPosition.z < baseHeight - outlineSunkenTolerance) {
        return clip;
    }
    vec3 camera = frame.cameraPositionAndNearPlane.xyz;
    float floorHeight = baseHeight + outlineFloorClearance;
    if (camera.z <= floorHeight + outlineSunkenTolerance) {
        return clip;
    }
    // The world-space step that moves the clip position by clipOffset and
    // leaves its depth along the view alone: the rows of clipFromWorld that
    // produce x, y and w, solved for that step.
    mat4 clipFromWorld = frame.clipFromWorld;
    mat3 rows = transpose(mat3(
        vec3(clipFromWorld[0].x, clipFromWorld[1].x, clipFromWorld[2].x),
        vec3(clipFromWorld[0].y, clipFromWorld[1].y, clipFromWorld[2].y),
        vec3(clipFromWorld[0].w, clipFromWorld[1].w, clipFromWorld[2].w)));
    if (abs(determinant(rows)) < 0.0000001) {
        return clip;
    }
    vec3 pushed = worldPosition + inverse(rows) * vec3(clipOffset, 0.0);
    if (pushed.z >= floorHeight) {
        return clip;
    }
    float alongSight = (camera.z - floorHeight) / (camera.z - pushed.z);
    return frame.clipFromWorld *
        vec4(camera + (pushed - camera) * alongSight, 1.0);
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
    if (draw.passData[0].z <= 0.0) {
        // No width: this draw is the depth of the solid itself, written for
        // the passes that come after it. The solid's own colour is drawn by
        // another shader, whose arithmetic need not round the way this one
        // does, so the mask sits a hair behind the surface it stands for and
        // can never win the depth test against it.
        clip.z += outlineDepthMaskBias * clip.w;
        gl_Position = clip;
        outFaceCoordU = 0.0;
        outFaceCoordV = 0.0;
        return;
    }
    vec4 clipNormal = frame.clipFromWorld * vec4(worldNormal, 0.0);
    vec2 extent = draw.passData[0].xy;
    vec2 direction = (clipNormal.xy * clip.w - clip.xy * clipNormal.w) * extent;
    float magnitude = length(direction);
    if (magnitude > 0.00001 && clip.w > 0.0) {
        vec2 clipOffset = direction / magnitude *
            (2.0 * draw.passData[0].z / extent) * clip.w;
        clip.xy += clipOffset;
        clip = outlineRestingOnFloor(
            clip, clipOffset, worldPosition, world[3].z);
    }
    gl_Position = clip;
    outFaceCoordU = 0.0;
    outFaceCoordV = 0.0;
}
