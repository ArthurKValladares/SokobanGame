#ifndef SOKOBAN_FLAME_STREAM_GLSL
#define SOKOBAN_FLAME_STREAM_GLSL

float flameHash(vec2 cell)
{
    vec3 p = fract(vec3(cell.xyx) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float flameNoise(vec2 position)
{
    vec2 cell = floor(position);
    vec2 local = fract(position);
    vec2 blend = local * local * (3.0 - 2.0 * local);
    return mix(mix(flameHash(cell), flameHash(cell + vec2(1, 0)), blend.x),
        mix(flameHash(cell + vec2(0, 1)), flameHash(cell + vec2(1, 1)), blend.x), blend.y);
}

float flameBillows(vec2 position)
{
    // Broad, smooth lobes dominate. Small detail modulates their folds rather
    // than punching little holes through the fire and making it sparkle.
    return flameNoise(position) * 0.65 +
        flameNoise(position * 2.03 + vec2(7.1, 3.7)) * 0.25 +
        flameNoise(position * 4.07 + vec2(2.8, 9.2)) * 0.10;
}

vec4 flameStreamColor(vec2 uv, vec4 flow, vec4 tint)
{
    float along = flow.y + uv.y * flow.z;
    float advected = along - flow.x * 4.2;
    float across = uv.x * 2.0 - 1.0;
    vec2 position = vec2(across * 3.0 + flow.w, advected * 4.5);
    vec2 warp = vec2(flameNoise(position * 0.55 + vec2(13.1, 4.7)),
        flameNoise(position * 0.55 + vec2(2.9, 17.3)));
    float billow = flameBillows(position + warp * 2.4);
    float folds = flameBillows(position * vec2(1.65, 0.72) + warp * 3.2);
    float sway = (flameNoise(vec2(advected * 1.8, flow.w + 17.0)) - 0.5) * 0.16;
    float radial = abs(across + sway);
    float nozzle = mix(0.48, 1.0, smoothstep(0.0, 0.65, along));
    float radius = (0.46 + billow * 0.40) * nozzle;

    // A solid central jet feeds broad curling tongues. The noisy density only
    // breaks up the outside, leaving the hot interior connected throughout.
    float density = (radius - radial) * 2.8 + (folds - 0.48) * 0.72;
    float feather = max(fwidth(density) * 1.15, 0.06);
    float opacity = smoothstep(-feather, 0.18 + feather, density);
    float heat = clamp(1.0 - radial / max(radius, 0.01) +
        (folds - 0.5) * 0.72 + (billow - 0.5) * 0.34, 0.0, 1.25);
    vec3 fire = mix(vec3(1.0, 0.10, 0.003), vec3(1.0, 0.64, 0.018),
        smoothstep(0.02, 0.56, heat));
    fire = mix(fire, vec3(1.0, 0.98, 0.28), smoothstep(0.48, 1.12, heat));
    fire *= 0.82 + folds * 0.32;
    return vec4(fire * tint.rgb, opacity * tint.a);
}

#endif
