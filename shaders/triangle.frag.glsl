#version 460

layout(set = 0, binding = 0) uniform sampler2D shadowMap;
layout(set = 0, binding = 1) uniform sampler2D sceneColor;
layout(set = 0, binding = 2) uniform sampler2D modelTextures[MODEL_TEXTURE_COUNT];
layout(set = 0, binding = 3) uniform sampler2D uiFont;
layout(set = 0, binding = 4) uniform sampler2D titleBackground;
layout(set = 0, binding = 8) uniform samplerCubeArray pointShadowMaps;

layout(location = 0) in vec4 inShadowPosition;
layout(location = 1) in float inFaceCoordU;
layout(location = 2) in float inFaceCoordV;
layout(location = 3) in vec3 inNormal;
layout(location = 4) flat in uint inTextureIndex;
layout(location = 5) flat in uint inMaterialFlags;
layout(location = 6) in vec3 inWorldPosition;
layout(location = 7) flat in uint inDrawInstance;
layout(location = 0) out vec4 outColor;

// One draw's parameters, read back by instance index. T1 moved these out of
// push constants so that consecutive draws sharing a pipeline can collapse
// into a single instanced draw.
struct DrawInstance
{
    vec4 vertices[4];
    vec4 passData[4];
    vec4 color;
    vec4 normalAndAmbientRed;
    vec4 sunDirectionAndAmbientGreen;
    vec4 sunRadianceAndAmbientBlue;
    vec4 shadowOptions;
    vec4 materialOptions;
    vec4 gridColor;
    vec4 textureOptions;
};
layout(std430, set = 0, binding = 10) readonly buffer DrawInstances
{
    DrawInstance instances[];
} drawInstances;

#define draw drawInstances.instances[inDrawInstance]


struct PointLightData
{
    vec4 positionAndRange;
    vec4 colorAndIntensity;
    vec4 shadowOptions;
};
layout(std140, set = 0, binding = 7) uniform SceneFrame
{
    mat4 clipFromWorld;
    mat4 shadowFromWorld;
    vec4 cameraPositionAndNearPlane;
    PointLightData pointLights[8];
    vec4 pointLightMeta;
} frame;

const int MATERIAL_MODE_PROCEDURAL_TEXTURE = 5;

vec3 gaussianBlurredScene(vec2 uv)
{
    const float weights[5] = float[5](1.0, 4.0, 6.0, 4.0, 1.0);
    vec2 viewportSize = vec2(textureSize(sceneColor, 0));
    vec2 texel = abs(draw.materialOptions.w) / viewportSize;
    vec3 result = vec3(0.0);
    float totalWeight = 0.0;

    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            float weight = weights[x + 2] * weights[y + 2];
            result += texture(sceneColor, uv + vec2(float(x), float(y)) * texel).rgb * weight;
            totalWeight += weight;
        }
    }

    return result / totalWeight;
}

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

void applyEditorPreviewDither()
{
    if (draw.materialOptions.w >= 0.0) {
        return;
    }

    const float pixelScale = 2.0;
    const float coverage = 0.56;
    ivec2 ditherPixel = ivec2(floor(gl_FragCoord.xy / pixelScale));
    if (bayer8x8(ditherPixel) >= coverage) {
        discard;
    }
}

float gridMask()
{
    if (draw.gridColor.a <= 0.0 || draw.shadowOptions.w <= 0.0 || draw.materialOptions.y <= 0.0 || draw.materialOptions.z <= 0.0) {
        return 0.0;
    }

    vec2 faceCoord = vec2(inFaceCoordU, inFaceCoordV);
    vec2 wrapped = fract(faceCoord);
    vec2 distanceToLine = min(wrapped, 1.0 - wrapped);
    vec2 coordPerPixel = max(fwidth(faceCoord), vec2(0.00001));
    vec2 halfWidth = coordPerPixel * draw.shadowOptions.w * 0.5;
    vec2 feather = coordPerPixel;
    vec2 line = 1.0 - smoothstep(halfWidth, halfWidth + feather, distanceToLine);
    return max(line.x, line.y) * draw.gridColor.a;
}

float shadowFactor(vec4 shadowPosition, float diffuse)
{
    if (draw.shadowOptions.x <= 0.5 || diffuse <= 0.0 || abs(shadowPosition.w) <= 0.0001) {
        return 1.0;
    }

    vec3 shadowCoord = shadowPosition.xyz / shadowPosition.w;
    vec2 shadowUv = shadowCoord.xy * 0.5 + 0.5;
    if (any(lessThan(shadowUv, vec2(0.0))) ||
        any(greaterThan(shadowUv, vec2(1.0))) ||
        shadowCoord.z < 0.0 ||
        shadowCoord.z > 1.0) {
        return 1.0;
    }

    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float shadowedSamples = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float closestDepth = texture(shadowMap, shadowUv + vec2(float(x), float(y)) * texel).r;
            shadowedSamples += shadowCoord.z - draw.shadowOptions.z > closestDepth ? 1.0 : 0.0;
        }
    }

    float shadowAmount = shadowedSamples / 9.0;
    return 1.0 - shadowAmount * draw.shadowOptions.y;
}

float pointShadowWorldDistance(
    float depth, float nearPlane, float farPlane)
{
    float denominator = farPlane - depth * (farPlane - nearPlane);
    return denominator > 0.000001
        ? farPlane * nearPlane / denominator
        : farPlane;
}

float pointShadowFactor(
    int lightIndex, vec3 fromLight, vec3 surfaceNormal)
{
    PointLightData light = frame.pointLights[lightIndex];
    if (light.shadowOptions.x <= 0.5) {
        return 1.0;
    }
    const float nearPlane = 0.05;
    float farPlane = max(light.positionAndRange.w, nearPlane + 0.001);
    float majorDistance = max(
        abs(fromLight.x), max(abs(fromLight.y), abs(fromLight.z)));
    if (majorDistance <= nearPlane || majorDistance >= farPlane) {
        return 1.0;
    }
    float texelAngle = 2.0 / float(textureSize(pointShadowMaps, 0).x);
    vec3 direction = normalize(fromLight);
    // The authored bias is a world-space minimum. Increase it at grazing
    // angles, where rasterized depth changes fastest across the surface.
    float facing = clamp(dot(normalize(surfaceNormal), -direction), 0.0, 1.0);
    float worldBias = max(light.shadowOptions.z, 0.0) *
        (1.0 + 2.0 * (1.0 - facing));
    vec3 tangent = normalize(cross(
        abs(direction.z) < 0.9 ? vec3(0.0, 0.0, 1.0)
                               : vec3(0.0, 1.0, 0.0),
        direction));
    vec3 bitangent = cross(direction, tangent);
    vec3 offsets[5] = vec3[5](
        vec3(0.0), tangent, -tangent, bitangent, -bitangent);
    float shadowed = 0.0;
    for (int sampleIndex = 0; sampleIndex < 5; ++sampleIndex) {
        vec3 sampleDirection = direction +
            offsets[sampleIndex] * texelAngle;
        float closestDepth = texture(
            pointShadowMaps,
            vec4(sampleDirection, light.shadowOptions.y)).r;
        float closestDistance = pointShadowWorldDistance(
            closestDepth, nearPlane, farPlane);
        shadowed += majorDistance - worldBias > closestDistance
            ? 1.0
            : 0.0;
    }
    return 1.0 - shadowed / 5.0 *
        clamp(light.shadowOptions.w, 0.0, 1.0);
}

void main()
{
    applyEditorPreviewDither();

    vec4 materialColor = draw.color;
    int materialMode = int(draw.textureOptions.x + 0.5);
    if (materialMode == 3) {
        vec2 uv = draw.gridColor.xy + vec2(inFaceCoordU, inFaceCoordV);
        materialColor.a *= texture(uiFont, uv).r;
    } else if (materialMode == 6) {
        vec2 uv = draw.gridColor.xy + vec2(inFaceCoordU, inFaceCoordV);
        materialColor *= texture(sceneColor, uv);

        // Scene-image UI is the preserved main view composited over the
        // preview. A signed-distance rounded rectangle makes the preview's
        // outside edge genuinely transparent and gives corners one smooth,
        // continuous curve instead of a stack of rectangular bands.
        vec2 uvSpan = max(draw.materialOptions.yz, vec2(0.00001));
        vec2 localPosition =
            vec2(inFaceCoordU, inFaceCoordV) / uvSpan;
        vec2 rectSize = max(draw.shadowOptions.xy, vec2(1.0));
        float featherWidth = max(draw.shadowOptions.z, 0.001);
        float cornerRadius = clamp(
            draw.shadowOptions.w,
            0.0,
            min(rectSize.x, rectSize.y) * 0.5);
        vec2 centeredPosition =
            localPosition * rectSize - rectSize * 0.5;
        vec2 distanceToCorner = abs(centeredPosition) -
            (rectSize * 0.5 - vec2(cornerRadius));
        float signedDistance =
            length(max(distanceToCorner, vec2(0.0))) +
            min(max(distanceToCorner.x, distanceToCorner.y), 0.0) -
            cornerRadius;
        float normalizedDistance = clamp(
            -signedDistance / featherWidth,
            0.0,
            1.0);
        float endpointSoftened = smoothstep(
            0.0,
            1.0,
            normalizedDistance);
        // A normalized logarithmic remap lifts the quiet first part of the
        // Hermite curve and softens its abrupt-looking middle without losing
        // the zero-slope endpoints that prevent visible seams.
        const float logarithmicStrength = 2.0;
        float previewOpacity = log(
            1.0 + logarithmicStrength * endpointSoftened) /
            log(1.0 + logarithmicStrength);
        materialColor.a *= 1.0 - previewOpacity;
    } else if (materialMode == 4) {
        vec2 uv = draw.gridColor.xy + vec2(inFaceCoordU, inFaceCoordV);
        materialColor *= texture(titleBackground, uv);
    } else if (materialMode == 7) {
        vec2 uv = draw.gridColor.xy + vec2(inFaceCoordU, inFaceCoordV);
        int textureIndex = clamp(
            int(draw.textureOptions.y + 0.5) - 1,
            0,
            MODEL_TEXTURE_COUNT - 1);
        materialColor *= texture(modelTextures[textureIndex], uv);
    } else if (materialMode == 2) {
        if (inTextureIndex != 0u) {
            int textureIndex = clamp(int(inTextureIndex - 1u), 0, MODEL_TEXTURE_COUNT - 1);
            vec2 uv = vec2(inFaceCoordU, inFaceCoordV);
            if ((inMaterialFlags & 1u) != 0u) {
                uv.y = fract(uv.y + draw.materialOptions.y);
            }
            materialColor *= texture(modelTextures[textureIndex], uv);
        }
    } else if (materialMode == MATERIAL_MODE_PROCEDURAL_TEXTURE) {
        // Procedural quads use a one-based texture handle because zero means
        // that no runtime texture was resolved.
        float selectedTexture = draw.textureOptions.y - 1.0;
        int textureIndex = clamp(
            int(selectedTexture + 0.5),
            0,
            MODEL_TEXTURE_COUNT - 1);
        materialColor *= texture(modelTextures[textureIndex], vec2(inFaceCoordU, inFaceCoordV));
    } else if (materialMode == 1) {
        int textureIndex = clamp(
            int(draw.materialOptions.z + 0.5),
            0,
            MODEL_TEXTURE_COUNT - 1);
        materialColor *= texture(modelTextures[textureIndex], vec2(inFaceCoordU, inFaceCoordV));
    }
    vec3 color = mix(materialColor.rgb, draw.gridColor.rgb, gridMask());
    if (length(inNormal) > 0.0001) {
        vec3 normal = normalize(inNormal);
        vec3 lightDirection = length(draw.sunDirectionAndAmbientGreen.xyz) > 0.0001
            ? normalize(draw.sunDirectionAndAmbientGreen.xyz)
            : vec3(0.0, 0.0, 1.0);
        float rawDiffuse = dot(normal, lightDirection);
        float lambertDiffuse = max(rawDiffuse, 0.0);
        float wrappedDiffuse = clamp(rawDiffuse * 0.5 + 0.5, 0.0, 1.0);
        float diffuse = mix(lambertDiffuse, wrappedDiffuse * wrappedDiffuse, 0.65);
        vec3 ambient = vec3(
            draw.normalAndAmbientRed.w,
            draw.sunDirectionAndAmbientGreen.w,
            draw.sunRadianceAndAmbientBlue.w);
        float shadow = shadowFactor(inShadowPosition, lambertDiffuse);
        float skyFill = smoothstep(-0.35, 1.0, normal.z);
        vec3 pointDiffuseLighting = vec3(0.0);
        vec3 pointSpecularLighting = vec3(0.0);
        float specularStrength = max(draw.textureOptions.z, 0.0);
        // The real direction from the surface to the camera. This used
        // to be a compiled-in constant matching one fixed isometric camera,
        // so every specular highlight was correct only from that angle and
        // wrong everywhere else the camera could go.
        vec3 viewDirection = normalize(
            frame.cameraPositionAndNearPlane.xyz - inWorldPosition);
        int pointLightCount = clamp(int(frame.pointLightMeta.x + 0.5), 0, 8);
        for (int lightIndex = 0; lightIndex < pointLightCount; ++lightIndex) {
            PointLightData pointLight = frame.pointLights[lightIndex];
            vec3 toLight = pointLight.positionAndRange.xyz - inWorldPosition;
            float distanceToLight = length(toLight);
            float range = max(pointLight.positionAndRange.w, 0.001);
            if (distanceToLight <= 0.0001 || distanceToLight >= range) {
                continue;
            }
            vec3 pointDirection = toLight / distanceToLight;
            float pointLambert = max(dot(normal, pointDirection), 0.0);
            if (pointLambert <= 0.0) {
                continue;
            }
            float normalizedDistance = distanceToLight / range;
            float rangeWindow = max(
                1.0 - normalizedDistance * normalizedDistance *
                    normalizedDistance * normalizedDistance,
                0.0);
            float attenuation = rangeWindow * rangeWindow /
                max(distanceToLight * distanceToLight, 0.04);
            float pointShadow = pointShadowFactor(
                lightIndex, -toLight, normal);
            vec3 radiance = pointLight.colorAndIntensity.rgb *
                pointLight.colorAndIntensity.w * attenuation;
            pointDiffuseLighting += radiance * pointLambert * pointShadow;
            if (specularStrength > 0.0) {
                vec3 pointHalf = normalize(pointDirection + viewDirection);
                float pointSpecular = pow(
                    max(dot(normal, pointHalf), 0.0),
                    max(draw.textureOptions.w, 1.0));
                pointSpecularLighting += radiance * pointSpecular *
                    pointShadow * specularStrength;
            }
        }
        vec3 diffuseLighting = ambient * (1.0 + skyFill * 0.35) +
            draw.sunRadianceAndAmbientBlue.rgb * diffuse * shadow +
            pointDiffuseLighting;
        color *= diffuseLighting;

        if (specularStrength > 0.0 && lambertDiffuse > 0.0) {
            vec3 halfDirection = normalize(lightDirection + viewDirection);
            float specularPower = max(draw.textureOptions.w, 1.0);
            float specular = pow(max(dot(normal, halfDirection), 0.0), specularPower);
            specular *= smoothstep(0.0, 0.2, lambertDiffuse) * shadow * specularStrength;
            color += draw.sunRadianceAndAmbientBlue.rgb * specular * materialColor.a;
        }
        color += pointSpecularLighting * materialColor.a;

    }

    int editorHighlight = int(draw.textureOptions.y + 0.5);
    if (draw.gridColor.w < 0.0 && editorHighlight > 0) {
        vec3 highlightColor = editorHighlight == 2
            ? vec3(1.0, 0.48, 0.08)
            : vec3(0.08, 0.88, 1.0);
        float tintStrength = editorHighlight == 2 ? 0.48 : 0.38;
        float pulse = 0.5 + 0.5 * sin(draw.materialOptions.w * 3.5);

        // A narrow screen-space sweep gives the opaque tint some life without
        // changing the model's geometry, texture, depth, or silhouette.
        float sweepPhase = fract(
            (gl_FragCoord.x + gl_FragCoord.y) * 0.0025 -
            draw.materialOptions.w * 0.55);
        float glimmer = 1.0 - smoothstep(0.0, 0.075, abs(sweepPhase - 0.5));

        color = mix(color, highlightColor, tintStrength + pulse * 0.04);
        color += mix(vec3(1.0), highlightColor, 0.35) * glimmer * 0.32;
        outColor = vec4(color, 1.0);
        return;
    }

    if (draw.materialOptions.x > 0.5) {
        vec2 uv = gl_FragCoord.xy / vec2(textureSize(sceneColor, 0));
        vec3 blurred = gaussianBlurredScene(uv);
        outColor = vec4(mix(blurred, color, materialColor.a), 1.0);
        return;
    }

    outColor = vec4(color, materialColor.a);
}
