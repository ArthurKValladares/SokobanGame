#ifndef SOKOBAN_GROUND_SEAMS_GLSL
#define SOKOBAN_GROUND_SEAMS_GLSL

// The board's grid, drawn as the joints between flagstones instead of as
// lines. Requires SceneFrame.glsl; groundSeamLanes in SceneDrawLanes.hpp
// packs the two frame lanes read here.
//
// Every cell holds one stone. The borders between cells wander, and both
// neighbours follow the same wandering line, so a joint is one crack and not
// two. Each stone then stands back from its borders by its own ragged margin
// and loses its corners to a chip, which is what opens the crevice and the
// small pockets where four stones meet. Everything is a function of the world
// position alone: no texture, nothing per tile, the same joint from whichever
// cell a fragment belongs to, and it stays put while the camera moves.
//
// All of the noise is values at whole knots joined by straight runs. That is
// what makes a joint a crack with corners in it and not a wave.
//
// A joint only changes the ground's colour. Leaning the normal over the
// stones' edges was tried: from screen-space derivatives it comes out in
// steps two pixels wide, and under this lighting it darkens both sides of a
// joint, so the light on an edge is painted instead.

uint groundSeamHash(ivec2 point, uint salt)
{
    uint hash = uint(point.x) * 1664525u ^ uint(point.y) * 1013904223u ^ salt;
    hash ^= hash >> 16;
    hash *= 2246822519u;
    hash ^= hash >> 13;
    hash *= 3266489917u;
    return hash ^ (hash >> 16);
}

float groundSeamUnit(ivec2 point, uint salt)
{
    return float(groundSeamHash(point, salt) & 65535u) / 65535.0;
}

// -1..1 along one line of the lattice, with a knot at every whole `along`.
// The value holds for the first `hold` of the way from one knot to the next
// and crosses over in the rest, so a hold makes a run and then a jog.
float groundSeamJag(int line, float along, uint salt, float hold)
{
    float knot = floor(along);
    ivec2 key = ivec2(line, int(knot));
    float crossing = clamp((along - knot - hold) / (1.0 - hold), 0.0, 1.0);
    return mix(groundSeamUnit(key, salt),
        groundSeamUnit(key + ivec2(0, 1), salt), crossing) * 2.0 - 1.0;
}

float groundSeamJag(int line, float along, uint salt)
{
    return groundSeamJag(line, along, salt, 0.0);
}

// How far a border has strayed from its straight line where `along` crosses
// it: long slanting runs, shorter runs with a jog between them on top, and a
// fine unevenness over both. A joint is seldom parallel to its line for long.
float groundSeamWander(int line, float along, uint salt)
{
    return frame.groundSeamShape.y * (
        0.5 * groundSeamJag(line, along * 1.3 + 0.37, salt) +
        0.3 * groundSeamJag(line, along * 3.7 + 0.11, salt + 1u, 0.5) +
        0.2 * groundSeamJag(line, along * 9.7 + 0.5, salt + 2u));
}

// 1 where a border shows at its fullest, less where it has all but closed.
float groundSeamPresence(int line, float along, uint salt)
{
    return 1.0 - 0.6 * smoothstep(0.15, 0.85,
        0.5 + 0.5 * groundSeamJag(line, along * 1.7 + 0.2, salt));
}

struct GroundSeam
{
    // How much of the crevice's tint the ground takes here.
    float shade;
    // How much of the worn rim's paleness it takes.
    float rim;
};

// footprint is the width of a pixel in tiles. towardLight is the way to the
// sun along the ground, shorter the higher the sun stands.
GroundSeam groundSeamAt(vec2 position, float footprint, vec2 towardLight)
{
    GroundSeam result = GroundSeam(0.0, 0.0);
    float width = frame.groundSeamShape.x;
    float softness = frame.groundSeamShape.z;
    float chip = width * 2.0;

    vec2 nearestLine = floor(position + 0.5);
    vec2 offLine = abs(position - nearestLine);
    // Most of a stone is too far from any border for the rest to matter.
    float reach = frame.groundSeamShape.y + width * 1.5 + chip + softness;
    if (min(offLine.x, offLine.y) > min(reach, 0.5)) {
        return result;
    }

    // A border carries its wander only to the middle of the cells beside it,
    // where the next border's takes over.
    vec2 carried = 1.0 - smoothstep(0.25, 0.5, offLine);
    ivec2 wanderLine = ivec2(nearestLine);
    vec2 stone = position - carried * vec2(
        groundSeamWander(wanderLine.x, position.y, 11u),
        groundSeamWander(wanderLine.y, position.x, 17u));

    vec2 cellOrigin = floor(stone);
    vec2 local = stone - cellOrigin;
    ivec2 cell = ivec2(cellOrigin);
    bvec2 pastMiddle = greaterThan(local, vec2(0.5));
    // The border this fragment is nearest on each axis, and how far it is.
    ivec2 line = cell + ivec2(pastMiddle);
    vec2 toBorder = mix(local, 1.0 - local, vec2(pastMiddle));
    vec2 presence = vec2(
        groundSeamPresence(line.x, stone.y, 21u),
        groundSeamPresence(line.y, stone.x, 22u));

    // The stone's own margin on those two sides. A stone's west and east,
    // or south and north, are different edges and take different seeds.
    ivec2 side = ivec2(pastMiddle.x ? 1 : 0, pastMiddle.y ? 3 : 2);
    vec2 ragged = vec2(
        0.6 * groundSeamJag(cell.x * 4 + side.x, stone.y * 4.7, 31u) +
            0.4 * groundSeamJag(cell.x * 4 + side.x, stone.y * 11.3, 41u),
        0.6 * groundSeamJag(cell.y * 4 + side.y, stone.x * 4.7, 33u) +
            0.4 * groundSeamJag(cell.y * 4 + side.y, stone.x * 11.3, 43u));
    vec2 margin = 0.5 * width * presence * clamp(1.0 + 0.8 * ragged, 0.0, 3.0);
    vec2 inside = toBorder - margin;

    // The corner comes off along a straight cut of its own size and slant.
    ivec2 corner = ivec2(
        line.x * 4 + (pastMiddle.x ? 1 : 0) + (pastMiddle.y ? 2 : 0), line.y);
    float cut = groundSeamUnit(corner, 51u);
    float slant = (0.22 + 0.56 * groundSeamUnit(corner, 52u)) * 1.57079632679;
    vec2 across = vec2(cos(slant), sin(slant));
    float insideCut = dot(inside, across) -
        chip * (0.1 + 0.9 * cut * cut * cut) * max(across.x, across.y);
    // Positive within the stone, negative in the crevice.
    float distanceIn = min(min(inside.x, inside.y), insideCut);

    // Near a corner the two borders share the fragment. Weighing them by the
    // borders themselves, not by the stone, keeps both neighbours agreeing.
    float nearerUpright = smoothstep(-0.06, 0.06, toBorder.y - toBorder.x);
    float strength = mix(0.5, 1.0, mix(presence.y, presence.x, nearerUpright));

    // The way out of the stone over its nearest edge. An edge that looks
    // toward the sun catches it along its rim; one that looks away lies in
    // its own shade. With the sun overhead every edge has a little of both.
    vec2 outward = mix(vec2(-1.0), vec2(1.0), vec2(pastMiddle)) *
        vec2(nearerUpright, 1.0 - nearerUpright);
    float lit = smoothstep(-0.35, 0.45,
        dot(outward, towardLight) / max(length(outward), 0.0001));

    // The crevice is full at the border and gone a little way onto the
    // stone. Past it the shaded edge goes on darkening the stone faintly.
    float feather = max(footprint * 0.75, width * 0.25);
    float crevice = 1.0 - smoothstep(-feather, feather, distanceIn);
    float falloff = 1.0 - smoothstep(0.0, softness, distanceIn);
    result.shade = max(crevice,
        falloff * falloff * mix(0.3, 0.08, lit)) * strength;

    // The rim is a narrow band right at the stone's edge, broken along its
    // length.
    float rimWidth = max(softness * 0.3, footprint * 1.5);
    float worn = smoothstep(0.25, 0.75, 0.5 + 0.5 * groundSeamJag(
        7, (position.x + position.y * 1.618) * 5.1, 61u));
    result.rim = smoothstep(0.0, feather, distanceIn) *
        (1.0 - smoothstep(feather, feather + rimWidth, distanceIn)) *
        (0.35 + 0.65 * worn) * lit * strength;
    return result;
}

#endif
