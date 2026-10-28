#ifndef SOKOBAN_GROUND_DEBRIS_GLSL
#define SOKOBAN_GROUND_DEBRIS_GLSL

// _GROUND_DEBRIS is constant across one detached rock, so this is a rigid
// translation. Normals, tangents and both texture charts stay untouched.
// animation: x=simulation clock, y=stable tile seed, z=enabled.
vec3 groundDebrisOffset(float rockSeed, vec4 animation)
{
    if (rockSeed <= 0.0 || animation.z <= 0.0) return vec3(0.0);
    vec3 random = fract(vec3(rockSeed * 17.137, rockSeed * 43.719,
        rockSeed * 91.331) + animation.y * vec3(11.731, 29.173, 53.117));
    vec3 phase = random * 6.28318530718;
    vec3 rate = mix(vec3(0.43, 0.37, 0.62), vec3(0.81, 0.73, 1.08),
        fract(random.yzx * 7.173));
    vec3 primary = sin(animation.x * rate + phase);
    vec3 secondary = sin(animation.x * rate.zxy * 0.613 + phase.yzx * 1.731);
    // Full excursion <= .008/.008/.020 tiles. Independent frequencies and
    // a second slow wave keep neighboring fragments from sharing a loop.
    return vec3(0.008, 0.008, 0.020) * mix(vec3(0.6), vec3(1.0), random.zxy) *
        (primary * 0.78 + secondary * 0.22);
}

#endif
