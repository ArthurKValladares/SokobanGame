#ifndef SOKOBAN_ROCK_SCRATCHES_GLSL
#define SOKOBAN_ROCK_SCRATCHES_GLSL

float rockScratchHash(float value)
{
    float p = fract(value * 0.1031);
    p *= p + 33.33;
    return fract(p * (p + p));
}

float rockScratchNoise(float coordinate)
{
    float cell = floor(coordinate);
    float fraction = fract(coordinate);
    fraction = fraction * fraction * (3.0 - 2.0 * fraction);
    return mix(rockScratchHash(cell), rockScratchHash(cell + 1.0), fraction);
}

float rockScratchGroundNoise(vec2 coordinate)
{
    vec2 cell = floor(coordinate);
    vec2 fraction = fract(coordinate);
    fraction = fraction * fraction * (3.0 - 2.0 * fraction);
    float key = cell.x + cell.y * 157.0;
    return mix(mix(rockScratchHash(key), rockScratchHash(key + 1.0), fraction.x),
        mix(rockScratchHash(key + 157.0), rockScratchHash(key + 158.0), fraction.x),
        fraction.y);
}

// x = pale scuff, y = the narrow dark furrow beside a deeper scrape. Both are
// applied to ground albedo before lighting and conform to the actual cap.
vec2 rockScratchCoverage(vec3 position, vec2 worldDx, vec2 worldDy)
{
    vec2 coverage = vec2(0.0);
    int count = clamp(int(frame.rockScratchMeta.x + 0.5),
        0, frame.rockScratches.length());
    for (int index = 0; index < count; ++index) {
        RockScratchData scratch = frame.rockScratches[index];
        vec2 start = scratch.startAndAge.xy;
        vec2 end = scratch.endAndDuration.xy;
        if (abs(position.z - scratch.startAndAge.z) > 0.40 ||
            any(lessThan(position.xy, min(start, end) - 0.36)) ||
            any(greaterThan(position.xy, max(start, end) + 0.36))) continue;

        vec2 path = end - start;
        float pathLength = length(path);
        if (pathLength < 0.0001) continue;

        vec2 direction = path / pathLength;
        vec2 offset = position.xy - start;
        float along = dot(offset, direction);
        bool reverse = direction.x < 0.0 || (direction.x == 0.0 && direction.y < 0.0);
        // Give shared endpoints to just one segment of a continuous track.
        float alongCanonical = reverse ? pathLength - along : along;
        if (alongCanonical < 0.0 || alongCanonical >= pathLength) continue;

        // A drag deposits its beginning first. Interpolating birth time along
        // every segment also makes the tail disappear before the newer head.
        float age = scratch.startAndAge.w -
            along / pathLength * scratch.endAndDuration.w;
        if (age < 0.0 || age >= frame.rockScratchMeta.y) continue;
        float fade = 1.0 - smoothstep(
            frame.rockScratchMeta.z, frame.rockScratchMeta.y, age);

        // Opposite drags follow the same grooves. The travel direction above
        // still controls when each point on the path is scratched.
        if (reverse) {
            direction = -direction;
        }
        vec2 across = vec2(-direction.y, direction.x);
        float lateral = dot(offset, across);
        if (abs(lateral) > 0.36) continue;

        // World coordinates keep every variation continuous across split cell
        // segments. Ground patches change contact independently of each ridge
        // on the stone, leaving intermittent deeper gouges and lighter scuffs.
        float longitudinal = dot(position.xy, direction);
        float phase = dot(scratch.startAndAge.xy, across) * 11.0 +
            scratch.startAndAge.z * 2.7;
        float groundContact = rockScratchGroundNoise(position.xy * 5.3);
        float feather = max(abs(dot(worldDx, across)) +
            abs(dot(worldDy, across)), 0.001) * 0.65;
        const float offsets[5] = float[5](-0.235, -0.125, -0.025, 0.115, 0.245);
        const float widths[5] = float[5](0.008, 0.015, 0.010, 0.013, 0.006);
        const float strengths[5] = float[5](0.48, 1.0, 0.65, 0.88, 0.38);
        vec2 strokes = vec2(0.0);
        for (int line = 0; line < 5; ++line) {
            float seed = phase + float(line) * 3.71;
            float pressure = rockScratchNoise(longitudinal * 3.7 + seed * 13.0);
            float grain = rockScratchNoise(longitudinal * 31.0 + seed * 7.0);
            float contact = smoothstep(0.27, 0.57,
                pressure * 0.68 + groundContact * 0.32);
            float depth = mix(0.22, 1.0, pressure) * contact;
            float width = widths[line] * mix(0.45, 2.1, pressure) *
                mix(0.65, 1.15, grain);
            float wander = (rockScratchNoise(longitudinal * 2.1 + seed) - 0.5) * 0.05 +
                (rockScratchNoise(longitudinal * 15.0 + seed * 3.0) - 0.5) * 0.012;
            float signedDistance = lateral - offsets[line] - wander;
            float scuff = 1.0 - smoothstep(width, width + feather, abs(signedDistance));
            float furrow = 1.0 - smoothstep(width * 0.25,
                width * 0.25 + feather * 0.65, abs(signedDistance + width * 0.75));
            float scratched = strengths[line] * depth * mix(0.55, 1.0, grain);
            strokes = max(strokes, vec2(scuff * scratched,
                furrow * scratched * pressure));
        }
        // Each pass adds contrast to the existing groove, with diminishing
        // returns so heavily used tracks stay within the material's range.
        coverage += (1.0 - coverage) * strokes * (fade * frame.rockScratchMeta.w);
    }
    return coverage;
}

#endif
