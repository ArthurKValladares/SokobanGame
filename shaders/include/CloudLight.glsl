#ifndef SOKOBAN_CLOUD_LIGHT_GLSL
#define SOKOBAN_CLOUD_LIGHT_GLSL

// The sun, broken up as if by clouds passing overhead: a drifting pattern of
// gaps that dims sunlight wherever a cloud stands in its way. Surfaces and
// participating media evaluate the same pattern, and they evaluate it at the
// point where the sun ray through their own position crosses height zero.
// Every point along one ray therefore gets one answer, which is what puts a
// shaft in the air exactly over the pool of light it ends in.
//
// Only the sun is dimmed. Ambient light, point lights and the shadow map are
// not this file's business.
//
// Requires SceneFrame.glsl. cloudLightLanes in SceneDrawLanes.hpp packs the
// frame lanes read here, and cloudLightCoordinate there is the CPU reference
// for the function of the same name.

uint cloudLightHashBits(ivec2 cell)
{
    // Integer in, integer out, like the sky's hash: neighbouring cells must
    // agree exactly on the corners they share, and a floating hash can round
    // (cell + 1) differently once the GPU contracts a multiply-add.
    uvec2 p = uvec2(cell);
    uint h = (p.x * 0x8da6b343u) ^ (p.y * 0xd8163841u);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

float cloudLightHash(ivec2 cell)
{
    // 24 bits convert to float exactly, negative cells included.
    return float(cloudLightHashBits(cell) >> 8) * (1.0 / 16777216.0);
}

// Value noise in [0, 1] with a quintic fade, so that neither the pattern nor
// its slope has a crease along the lattice.
float cloudLightNoise(vec2 position)
{
    vec2 cellFloor = floor(position);
    ivec2 cell = ivec2(cellFloor);
    vec2 local = position - cellFloor;
    vec2 blend = local * local * local *
        (local * (local * 6.0 - 15.0) + 10.0);
    float lower = mix(
        cloudLightHash(cell),
        cloudLightHash(cell + ivec2(1, 0)),
        blend.x);
    float upper = mix(
        cloudLightHash(cell + ivec2(0, 1)),
        cloudLightHash(cell + ivec2(1, 1)),
        blend.x);
    return mix(lower, upper, blend.y);
}

// Where the sun ray through a world position crosses height zero, less how
// far the clouds have drifted. Constant along that ray.
vec2 cloudLightCoordinate(vec3 worldPosition)
{
    return worldPosition.xy +
        worldPosition.z * frame.cloudLightProjection.xy -
        frame.cloudLightShape.xy;
}

// One for open sky, zero under a cloud. Two octaves, the second turned so
// that the two lattices do not line up into visible rows.
float cloudLightGap(vec2 coordinate)
{
    const mat2 turn = mat2(0.8, 0.6, -0.6, 0.8);
    vec2 position = coordinate * frame.cloudLightProjection.z;
    float pattern = cloudLightNoise(position) * 0.68 +
        cloudLightNoise(turn * position * 2.03 + vec2(17.3, -9.1)) * 0.32;
    // The sum crowds around one half. Spread about that by two, its value
    // stays within 0.07 of its own percentile, which is what lets coverage
    // be read as a share of the sky.
    pattern = clamp((pattern - 0.5) * 2.0 + 0.5, 0.0, 1.0);
    float coverage = frame.cloudLightShape.z;
    float softness = max(frame.cloudLightShape.w, 0.001);
    return smoothstep(coverage - softness, coverage + softness, pattern);
}

// The share of the sun that reaches a surface: one in a gap, down to
// 1 - strength under a cloud. Surfaces see the broad pattern only, so the
// pools on a board stay large and calm.
float cloudLightOnSurface(vec3 worldPosition)
{
    float strength = frame.cloudLightStrength.x;
    if (strength <= 0.0) {
        return 1.0;
    }
    float gap = cloudLightGap(cloudLightCoordinate(worldPosition));
    return 1.0 - strength * (1.0 - gap);
}

// The share of the sun that reaches a point in a participating medium. Each
// medium brings its own strength, and a gain for the light that still gets
// through: breaking the sun up takes more than half of it away, and the gain
// gives part of that back inside the gaps, so the medium keeps most of its
// brightness and the shafts gain contrast. Surfaces have no gain.
//
// The air also sees a finer set of breaks inside the gaps, which splits one
// broad beam into several narrow ones. A line of sight crosses the sun's
// rays rather than following them, and gathers the light of everything it
// passes on the way: round breaks would mostly average out before they
// reached the eye. So these are drawn out along the direction sight sweeps
// across the pattern (cloudLightRays, from cloudLightRayAxis on the CPU),
// and each arrives as one clean ray. They drift a little faster than the
// clouds, so the rays shift against each other instead of sliding past as
// one picture.
//
// A gain below one is not a gain but a mark (cloudLightGainOfASurface on the
// CPU): the medium is dense enough to be read as a surface, and takes the
// light as one does, from the broad pattern only.
float cloudLightInMedium(vec3 worldPosition, float strength, float gain)
{
    if (strength <= 0.0) {
        return 1.0;
    }
    vec2 coordinate = cloudLightCoordinate(worldPosition);
    float gap = cloudLightGap(coordinate);
    if (gain < 1.0) {
        return 1.0 - strength * (1.0 - gap);
    }
    float detailStrength = frame.cloudLightStrength.y;
    if (detailStrength > 0.0) {
        vec2 along = frame.cloudLightRays.xy;
        vec2 across = vec2(-along.y, along.x);
        vec2 drifted = coordinate - frame.cloudLightShape.xy * 0.35;
        vec2 streak = vec2(
            dot(drifted, across),
            dot(drifted, along) * frame.cloudLightRays.z);
        float detail = cloudLightNoise(
            streak * frame.cloudLightProjection.w + vec2(-31.7, 12.9));
        // One octave crowds around one half less than two do; this spread
        // brings it as close to its percentile. The breaks then close half
        // of every gap, with an edge tighter than the clouds' own, because
        // a ray is a thin thing.
        detail = clamp((detail - 0.5) * 1.45 + 0.5, 0.0, 1.0);
        float softness = max(frame.cloudLightShape.w * 0.5, 0.001);
        gap *= 1.0 - detailStrength *
            (1.0 - smoothstep(0.5 - softness, 0.5 + softness, detail));
    }
    return (1.0 - strength * (1.0 - gap)) * gain;
}

#endif
