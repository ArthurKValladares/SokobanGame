#version 460

layout(set = 0, binding = 1) uniform sampler2D sceneColor;

layout(location = 1) in float inFaceCoordU;
layout(location = 2) in float inFaceCoordV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants
{
    vec4 vertices[4];
    vec4 shadowVertices[4];
    vec4 color;
    vec4 normalAndAmbientRed;
    vec4 sunDirectionAndAmbientGreen;
    vec4 sunRadianceAndAmbientBlue;
    vec4 shadowOptions;
    vec4 materialOptions;
    vec4 gridColor;
    vec4 textureOptions;
} pc;

float bayer8x8(ivec2 pixel)
{
    const float thresholds[64] = float[64](
         0.0, 48.0, 12.0, 60.0,  3.0, 51.0, 15.0, 63.0,
        32.0, 16.0, 44.0, 28.0, 35.0, 19.0, 47.0, 31.0,
         8.0, 56.0,  4.0, 52.0, 11.0, 59.0,  7.0, 55.0,
        40.0, 24.0, 36.0, 20.0, 43.0, 27.0, 39.0, 23.0,
         2.0, 50.0, 14.0, 62.0,  1.0, 49.0, 13.0, 61.0,
        34.0, 18.0, 46.0, 30.0, 33.0, 17.0, 45.0, 29.0,
        10.0, 58.0,  6.0, 54.0,  9.0, 57.0,  5.0, 53.0,
        42.0, 26.0, 38.0, 22.0, 41.0, 25.0, 37.0, 21.0);
    ivec2 wrapped = pixel & ivec2(7);
    return (thresholds[wrapped.y * 8 + wrapped.x] + 0.5) / 64.0;
}

vec2 organicRippleBands(vec2 position, float time)
{
    vec2 primaryWarp = vec2(
        sin(position.y * 0.83 + time * 0.72) +
            sin((position.x + position.y) * 0.41 - time * 0.37) * 0.50,
        cos(position.x * 0.79 - time * 0.65) +
            cos((position.x - position.y) * 0.47 + time * 0.42) * 0.50);
    vec2 detailWarp = vec2(
        sin(position.x * 2.17 - position.y * 1.43 + time * 1.03),
        cos(position.x * 1.61 + position.y * 2.31 - time * 0.91));
    vec2 warpedPosition =
        position + primaryWarp * 0.32 + detailWarp * 0.085;

    float waveA = sin(
        warpedPosition.x * 1.18 +
        sin(warpedPosition.y * 0.72 + time * 0.31) * 1.05);
    float waveB = sin(
        warpedPosition.y * 1.26 +
        cos(warpedPosition.x * 0.67 - time * 0.29) * 1.10);
    float waveC = sin(
        (warpedPosition.x + warpedPosition.y) * 0.63 +
        sin(
            (warpedPosition.x - warpedPosition.y) * 0.54 +
            time * 0.23));
    float contourField = waveA * 0.72 + waveB * 0.68 + waveC * 0.48;
    float distanceFromRipple = abs(contourField);
    float antialiasWidth = max(fwidth(contourField), 0.012);
    float softHalo = 1.0 - smoothstep(
        0.035 - antialiasWidth,
        0.300 + antialiasWidth,
        distanceFromRipple);
    float brightCenter = 1.0 - smoothstep(
        0.015 - antialiasWidth,
        0.095 + antialiasWidth,
        distanceFromRipple);
    return vec2(softHalo, brightCenter);
}

vec2 shorelineWave(
    float distanceToEdge,
    float alongEdge,
    float time,
    float phase,
    float nearDistance,
    float farDistance,
    float configuredFarThickness)
{
    float nearVariation =
        sin(alongEdge * 8.10 + time * 2.20 + phase) * 0.009 +
        sin(alongEdge * 15.30 - time * 1.45 + phase * 1.70) * 0.005 +
        sin(alongEdge * 24.70 + time * 0.83 + phase * 0.70) * 0.0025;
    float nearCrest = max(nearDistance + nearVariation, 0.005);
    float farVariation =
        sin(alongEdge * 6.30 - time * 1.65 + phase * 0.73) * 0.012 +
        sin(alongEdge * 13.70 + time * 0.88 - phase * 0.41) * 0.005;
    float farCrest = max(farDistance + farVariation, 0.005);
    float farThickness = max(
        configuredFarThickness *
            (0.88 +
                (sin(alongEdge * 11.70 - time * 1.10 + phase) *
                        0.5 +
                    0.5) *
                    0.24),
        0.001);
    float antialiasWidth = max(fwidth(distanceToEdge), 0.002);
    float nearFill = 1.0 - smoothstep(
        nearCrest - antialiasWidth,
        nearCrest + 0.016 + antialiasWidth,
        distanceToEdge);
    float farBand = 1.0 - smoothstep(
        max(farThickness - antialiasWidth, 0.0),
        farThickness + antialiasWidth,
        abs(distanceToEdge - farCrest));
    return clamp(vec2(nearFill, farBand), 0.0, 1.0);
}

vec2 shorelineFoam(
    uint shorelineMask,
    vec2 localPosition,
    vec2 surfaceSize,
    vec2 worldPosition,
    float time,
    float nearDistance,
    float farDistance,
    float farThickness)
{
    vec2 foam = vec2(0.0);
    if ((shorelineMask & 1u) != 0u) {
        foam = max(
            foam,
            shorelineWave(
                localPosition.y,
                worldPosition.x,
                time,
                0.0,
                nearDistance,
                farDistance,
                farThickness));
    }
    if ((shorelineMask & 2u) != 0u) {
        foam = max(
            foam,
            shorelineWave(
                surfaceSize.x - localPosition.x,
                worldPosition.y,
                time,
                1.7,
                nearDistance,
                farDistance,
                farThickness));
    }
    if ((shorelineMask & 4u) != 0u) {
        foam = max(
            foam,
            shorelineWave(
                surfaceSize.y - localPosition.y,
                worldPosition.x,
                time,
                3.4,
                nearDistance,
                farDistance,
                farThickness));
    }
    if ((shorelineMask & 8u) != 0u) {
        foam = max(
            foam,
            shorelineWave(
                localPosition.x,
                worldPosition.y,
                time,
                5.1,
                nearDistance,
                farDistance,
                farThickness));
    }
    return foam;
}

void main()
{
    if (pc.materialOptions.w < 0.0) {
        ivec2 ditherPixel = ivec2(floor(gl_FragCoord.xy * 0.5));
        if (bayer8x8(ditherPixel) >= 0.56) {
            discard;
        }
    }

    vec2 worldPosition = pc.gridColor.xy + vec2(inFaceCoordU, inFaceCoordV);
    float frequency = max(pc.textureOptions.x, 0.01);
    float time = pc.gridColor.z * pc.textureOptions.y;

    vec2 basePatternPosition = worldPosition * frequency;
    vec2 patternPosition =
        basePatternPosition +
        vec2(time * 0.10, -time * 0.075);
    vec2 caustics = organicRippleBands(patternPosition, time);
    vec2 darkPatternPosition =
        vec2(-patternPosition.y, patternPosition.x) +
        vec2(2.31, -1.73);
    vec2 darkCaustics =
        organicRippleBands(darkPatternPosition, time + 1.40);

    vec2 refractionFlow = 0.5 * vec2(
        sin(patternPosition.y * 1.13 + time) +
            cos(patternPosition.x * 0.71 - time * 0.81),
        cos(patternPosition.x * 1.07 - time * 0.92) +
            sin(patternPosition.y * 0.67 + time * 0.76));

    vec2 sceneSize = vec2(textureSize(sceneColor, 0));
    vec2 sceneUv = gl_FragCoord.xy / sceneSize;
    vec2 refractionOffset =
        refractionFlow * pc.textureOptions.z *
        vec2(sceneSize.y / max(sceneSize.x, 1.0), 1.0);
    vec3 refractedScene = texture(
        sceneColor,
        clamp(sceneUv + refractionOffset, vec2(0.001), vec2(0.999))).rgb;

    float bodyVariation =
        sin(patternPosition.x * 0.61 + time * 0.23) *
        cos(patternPosition.y * 0.73 - time * 0.19);
    vec3 waterTint = pc.color.rgb * mix(0.84, 1.08, bodyVariation * 0.5 + 0.5);
    vec3 waterColor = mix(
        refractedScene,
        waterTint,
        clamp(pc.color.a, 0.0, 0.95));

    float darkRippleStrength = clamp(
        darkCaustics.x * 0.34 + darkCaustics.y * 0.10,
        0.0,
        0.44);
    vec3 darkRippleColor = pc.color.rgb * 0.58;
    vec3 waterWithDarkRipples = mix(
        waterColor,
        darkRippleColor,
        darkRippleStrength);

    vec3 rippleColor = mix(
        vec3(0.22, 0.62, 0.80),
        vec3(0.70, 0.88, 0.94),
        caustics.y);
    float rippleStrength = clamp(
        caustics.x * 0.42 + caustics.y * 0.12,
        0.0,
        0.54);

    vec3 finalWaterColor =
        mix(waterWithDarkRipples, rippleColor, rippleStrength);
    uint shorelineMask =
        uint(max(pc.textureOptions.w, 0.0) + 0.5);
    vec2 foamLayers = shorelineFoam(
        shorelineMask,
        vec2(inFaceCoordU, inFaceCoordV),
        pc.materialOptions.yz,
        worldPosition,
        time,
        max(pc.materialOptions.x, 0.0),
        max(pc.gridColor.w, 0.0),
        max(pc.normalAndAmbientRed.x, 0.0));
    float farFoamOpacity =
        clamp(pc.normalAndAmbientRed.y, 0.0, 1.0);
    float foamStrength =
        max(foamLayers.x, foamLayers.y * farFoamOpacity);
    finalWaterColor = mix(
        finalWaterColor,
        vec3(0.94, 0.98, 1.00),
        foamStrength);

    outColor = vec4(finalWaterColor, 1.0);
}
