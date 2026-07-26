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

vec2 hash22(vec2 value)
{
    vec3 value3 =
        fract(vec3(value.xyx) * vec3(0.1031, 0.1030, 0.0973));
    value3 += dot(value3, value3.yxz + 33.33);
    return fract((value3.xx + value3.yz) * value3.zy);
}

float smoothValueNoise(vec2 position)
{
    vec2 cell = floor(position);
    vec2 local = fract(position);
    vec2 blend =
        local * local * local *
        (local * (local * 6.0 - 15.0) + 10.0);
    float negativeXNegativeY = hash22(cell).x;
    float positiveXNegativeY =
        hash22(cell + vec2(1.0, 0.0)).x;
    float negativeXPositiveY =
        hash22(cell + vec2(0.0, 1.0)).x;
    float positiveXPositiveY =
        hash22(cell + vec2(1.0, 1.0)).x;
    return mix(
        mix(
            negativeXNegativeY,
            positiveXNegativeY,
            blend.x),
        mix(
            negativeXPositiveY,
            positiveXPositiveY,
            blend.x),
        blend.y);
}

float broadWaterTone(
    vec2 worldPosition,
    float animationTime,
    float spatialFrequency,
    float transitionWidth,
    float speed)
{
    vec2 rotatedPosition = vec2(
        dot(worldPosition, vec2(0.82, -0.57)),
        dot(worldPosition, vec2(0.57, 0.82)));
    vec2 tonePosition =
        rotatedPosition * max(spatialFrequency, 0.01) +
        vec2(animationTime * speed, -animationTime * speed * 0.71);
    tonePosition += vec2(
        sin(tonePosition.y * 0.83 + animationTime * speed) * 0.34,
        cos(tonePosition.x * 0.77 - animationTime * speed) * 0.34);
    float coarseNoise = smoothValueNoise(tonePosition);
    float detailNoise = smoothValueNoise(
        tonePosition * 2.03 + vec2(13.71, -8.29));
    float toneNoise = mix(coarseNoise, detailNoise, 0.24);
    float antialiasWidth = max(
        fwidth(toneNoise) * 1.25,
        max(transitionWidth, 0.001));
    return smoothstep(
        0.5 - antialiasWidth,
        0.5 + antialiasWidth,
        toneNoise);
}

float waterTileBorderMask(
    vec2 worldPosition,
    float animationTime,
    float lineWidth,
    float warpAmplitude,
    float warpFrequency,
    float speed)
{
    float frequency = max(warpFrequency, 0.01);
    float phase = animationTime * speed;
    vec2 warpedPosition = worldPosition;
    warpedPosition.x += warpAmplitude * (
        sin(worldPosition.y * frequency + phase) * 0.68 +
        sin(
            worldPosition.y * frequency * 2.17 -
            phase * 0.61 +
            1.30) * 0.32);
    warpedPosition.y += warpAmplitude * (
        sin(worldPosition.x * frequency * 0.93 - phase * 0.87) * 0.68 +
        sin(
            worldPosition.x * frequency * 2.03 +
            phase * 0.53 -
            0.80) * 0.32);

    vec2 distanceToGrid = abs(
        fract(warpedPosition + vec2(0.5)) - vec2(0.5));
    float distanceToBorder = min(distanceToGrid.x, distanceToGrid.y);
    float antialiasWidth = max(fwidth(distanceToBorder) * 1.10, 0.00075);
    return 1.0 - smoothstep(
        max(lineWidth - antialiasWidth, 0.0),
        lineWidth + antialiasWidth,
        distanceToBorder);
}

float waterTileBorderBoardFade(
    vec2 worldPosition,
    vec2 boardOrigin,
    vec2 boardSize,
    float fadeDistance)
{
    if (any(lessThanEqual(boardSize, vec2(0.0)))) {
        return 0.0;
    }
    vec2 boardPosition = worldPosition - boardOrigin;
    vec2 distanceOutside = max(
        max(-boardPosition, boardPosition - boardSize),
        vec2(0.0));
    return 1.0 - smoothstep(
        0.0,
        max(fadeDistance, 0.001),
        length(distanceOutside));
}

vec2 triangularLatticeCoordinates(vec2 position)
{
    float row = position.y * 1.1547005;
    return vec2(position.x - row * 0.5, row);
}

vec2 irregularCellPoint(vec2 cell)
{
    vec2 latticePoint = vec2(
        cell.x + cell.y * 0.5,
        cell.y * 0.8660254);
    vec2 jitter = hash22(cell) - vec2(0.5);
    return latticePoint + vec2(
        jitter.x * 0.68 + jitter.y * 0.12,
        jitter.y * 0.58 - jitter.x * 0.10);
}

float cellDistanceWeight(vec2 cell)
{
    return mix(
        0.72,
        1.34,
        hash22(cell + vec2(19.17, 7.43)).x);
}

float irregularDistanceSquared(vec2 vectorToPoint, vec2 cell)
{
    vec2 randomAxis =
        hash22(cell + vec2(41.73, 23.19)) * 2.0 - vec2(1.0);
    randomAxis *= inversesqrt(
        max(dot(randomAxis, randomAxis), 0.001));
    vec2 perpendicular =
        vec2(-randomAxis.y, randomAxis.x);
    float aspect = mix(
        0.62,
        1.48,
        hash22(cell + vec2(3.11, 57.29)).y);
    vec2 oriented = vec2(
        dot(vectorToPoint, randomAxis) * aspect,
        dot(vectorToPoint, perpendicular) / aspect);
    return dot(oriented, oriented) *
        cellDistanceWeight(cell);
}

vec2 cellularRippleBands(
    vec2 position,
    float time,
    float crestHalfWidth,
    float haloWidth)
{
    vec2 firstWarp = vec2(
        sin(dot(position, vec2(0.52, 0.81)) + time * 0.22) +
            sin(dot(position, vec2(-0.91, 0.37)) - time * 0.16) * 0.58,
        cos(dot(position, vec2(0.76, -0.43)) - time * 0.19) +
            cos(dot(position, vec2(0.31, 0.94)) + time * 0.14) * 0.58);
    vec2 warpedOnce = position + firstWarp * 0.27;
    vec2 secondWarp = vec2(
        sin(
            dot(warpedOnce, vec2(2.37, -1.61)) +
            sin(dot(warpedOnce, vec2(0.83, 1.19)) - time * 0.24) *
                0.72 +
            time * 0.31),
        cos(
            dot(warpedOnce, vec2(1.47, 2.53)) +
            cos(dot(warpedOnce, vec2(-1.13, 0.71)) + time * 0.21) *
                0.68 -
            time * 0.27));
    vec2 warpedTwice =
        warpedOnce + secondWarp * 0.15;
    vec2 rippleWarp = vec2(
        sin(
            dot(warpedTwice, vec2(5.13, 2.27)) +
            sin(
                dot(warpedTwice, vec2(-2.11, 3.07)) -
                time * 0.34) *
                1.05 +
            time * 0.43),
        cos(
            dot(warpedTwice, vec2(-2.53, 5.37)) +
            cos(
                dot(warpedTwice, vec2(3.23, 1.79)) +
                time * 0.29) *
                0.98 -
            time * 0.39));
    vec2 samplePosition =
        warpedTwice + rippleWarp * 0.12;
    vec2 baseCell =
        floor(
            triangularLatticeCoordinates(samplePosition) +
            vec2(0.5));

    float nearestDistanceSquared = 1e20;
    float secondDistanceSquared = 1e20;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 cell = baseCell + vec2(x, y);
            vec2 vectorToPoint =
                irregularCellPoint(cell) - samplePosition;
            float distanceSquared =
                irregularDistanceSquared(vectorToPoint, cell);
            if (distanceSquared < nearestDistanceSquared) {
                secondDistanceSquared = nearestDistanceSquared;
                nearestDistanceSquared = distanceSquared;
            } else if (distanceSquared < secondDistanceSquared) {
                secondDistanceSquared = distanceSquared;
            }
        }
    }

    float distanceToBoundary = 0.5 * max(
        sqrt(secondDistanceSquared) -
            sqrt(nearestDistanceSquared),
        0.0);
    float antialiasWidth = clamp(
        fwidth(distanceToBoundary) * 0.90,
        0.003,
        0.012);
    float widthVariation = mix(
        0.72,
        1.28,
        sin(
            samplePosition.x * 1.73 -
            samplePosition.y * 1.21 +
            time * 0.18) *
                0.5 +
            0.5);
    float variedCrestHalfWidth =
        max(crestHalfWidth, 0.001) * widthVariation;
    float variedHaloWidth =
        max(haloWidth, crestHalfWidth) * widthVariation;
    float haloInnerWidth = min(
        variedCrestHalfWidth * 0.80,
        variedHaloWidth * 0.45);
    float softHalo = 1.0 - smoothstep(
        haloInnerWidth - antialiasWidth,
        variedHaloWidth + antialiasWidth,
        distanceToBoundary);
    float brightCenter = 1.0 - smoothstep(
        variedCrestHalfWidth - antialiasWidth,
        variedCrestHalfWidth + antialiasWidth,
        distanceToBoundary);
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

vec2 shorelineCornerWave(
    vec2 localPosition,
    vec2 cornerPosition,
    vec2 cornerWorldPosition,
    float time,
    float firstPhase,
    float secondPhase,
    float nearDistance,
    float farDistance,
    float farThickness)
{
    float distanceToCorner = length(localPosition - cornerPosition);
    return max(
        shorelineWave(
            distanceToCorner,
            cornerWorldPosition.x,
            time,
            firstPhase,
            nearDistance,
            farDistance,
            farThickness),
        shorelineWave(
            distanceToCorner,
            cornerWorldPosition.y,
            time,
            secondPhase,
            nearDistance,
            farDistance,
            farThickness));
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
    vec2 worldOrigin = worldPosition - localPosition;
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
    if ((shorelineMask & 16u) != 0u) {
        foam = max(
            foam,
            shorelineCornerWave(
                localPosition,
                vec2(0.0),
                worldOrigin,
                time,
                0.0,
                5.1,
                nearDistance,
                farDistance,
                farThickness));
    }
    if ((shorelineMask & 32u) != 0u) {
        foam = max(
            foam,
            shorelineCornerWave(
                localPosition,
                vec2(surfaceSize.x, 0.0),
                worldOrigin + vec2(surfaceSize.x, 0.0),
                time,
                0.0,
                1.7,
                nearDistance,
                farDistance,
                farThickness));
    }
    if ((shorelineMask & 64u) != 0u) {
        foam = max(
            foam,
            shorelineCornerWave(
                localPosition,
                surfaceSize,
                worldOrigin + surfaceSize,
                time,
                3.4,
                1.7,
                nearDistance,
                farDistance,
                farThickness));
    }
    if ((shorelineMask & 128u) != 0u) {
        foam = max(
            foam,
            shorelineCornerWave(
                localPosition,
                vec2(0.0, surfaceSize.y),
                worldOrigin + vec2(0.0, surfaceSize.y),
                time,
                3.4,
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
    float rippleCrestHalfWidth =
        max(pc.normalAndAmbientRed.z, 0.001);
    float rippleHaloWidth =
        max(pc.normalAndAmbientRed.w, rippleCrestHalfWidth);

    vec2 basePatternPosition = worldPosition * frequency;
    vec2 patternPosition =
        basePatternPosition +
        vec2(time * 0.10, -time * 0.075);
    vec2 caustics = cellularRippleBands(
        patternPosition,
        time,
        rippleCrestHalfWidth,
        rippleHaloWidth);
    vec2 secondaryPatternPosition =
        vec2(-patternPosition.y, patternPosition.x) +
        vec2(2.31, -1.73);
    float secondaryThicknessScale =
        max(pc.sunRadianceAndAmbientBlue.w, 0.05);
    vec2 secondaryCaustics = cellularRippleBands(
        secondaryPatternPosition,
        time + 1.40,
        rippleCrestHalfWidth * secondaryThicknessScale,
        rippleHaloWidth * secondaryThicknessScale);

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

    float bodyTone = broadWaterTone(
        worldPosition,
        pc.gridColor.z,
        pc.sunDirectionAndAmbientGreen.x,
        pc.sunDirectionAndAmbientGreen.w,
        pc.sunRadianceAndAmbientBlue.x);
    float bodyToneMultiplier = mix(
        pc.sunDirectionAndAmbientGreen.y,
        pc.sunDirectionAndAmbientGreen.z,
        bodyTone);
    vec3 waterTint =
        pc.color.rgb * bodyToneMultiplier;
    vec3 waterColor = mix(
        refractedScene,
        waterTint,
        clamp(pc.color.a, 0.0, 0.95));

    float secondaryRippleCoverage = clamp(
        secondaryCaustics.x * 0.12 +
            secondaryCaustics.y * 0.88,
        0.0,
        1.0);
    float secondaryRippleStrength =
        secondaryRippleCoverage *
        clamp(pc.shadowOptions.a, 0.0, 1.0);
    vec3 waterWithSecondaryRipples = mix(
        waterColor,
        pc.shadowOptions.rgb,
        secondaryRippleStrength);

    vec3 rippleColor = mix(
        vec3(0.22, 0.62, 0.80),
        vec3(0.70, 0.88, 0.94),
        caustics.y);
    float rippleHaloStrength =
        max(pc.sunRadianceAndAmbientBlue.y, 0.0);
    float rippleCrestStrength =
        max(pc.sunRadianceAndAmbientBlue.z, 0.0);
    float rippleStrength = clamp(
        caustics.x * rippleHaloStrength +
            caustics.y * rippleCrestStrength,
        0.0,
        min(rippleHaloStrength + rippleCrestStrength, 1.0));
    rippleStrength *= clamp(pc.shadowVertices[3].x, 0.0, 1.0);

    vec3 finalWaterColor =
        mix(waterWithSecondaryRipples, rippleColor, rippleStrength);
    float tileBorder = waterTileBorderMask(
        worldPosition,
        pc.gridColor.z,
        max(pc.shadowVertices[1].x, 0.0),
        max(pc.shadowVertices[1].y, 0.0),
        max(pc.shadowVertices[1].z, 0.01),
        pc.shadowVertices[1].w);
    tileBorder *= waterTileBorderBoardFade(
        worldPosition,
        pc.shadowVertices[2].xy,
        max(pc.shadowVertices[2].zw, vec2(0.0)),
        max(pc.normalAndAmbientRed.y, 0.0));
    finalWaterColor = mix(
        finalWaterColor,
        pc.shadowVertices[0].rgb,
        tileBorder * clamp(pc.shadowVertices[0].a, 0.0, 1.0));
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
    float nearFoamOpacity =
        clamp(pc.shadowVertices[3].y, 0.0, 1.0);
    float farFoamOpacity =
        clamp(pc.shadowVertices[3].z, 0.0, 1.0);
    float foamStrength =
        max(
            foamLayers.x * nearFoamOpacity,
            foamLayers.y * farFoamOpacity);
    finalWaterColor = mix(
        finalWaterColor,
        vec3(0.94, 0.98, 1.00),
        foamStrength);

    outColor = vec4(finalWaterColor, 1.0);
}
