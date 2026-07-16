#version 460

layout(set = 0, binding = 0) uniform sampler2D shadowMap;
layout(set = 0, binding = 1) uniform sampler2D sceneColor;
layout(set = 0, binding = 2) uniform sampler2D modelTextures[MODEL_TEXTURE_COUNT];

layout(location = 0) in vec4 inShadowPosition;
layout(location = 1) in float inFaceCoordU;
layout(location = 2) in float inFaceCoordV;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in float inTextureIndex;
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

vec3 gaussianBlurredScene(vec2 uv)
{
    const float weights[5] = float[5](1.0, 4.0, 6.0, 4.0, 1.0);
    vec2 viewportSize = vec2(textureSize(sceneColor, 0));
    vec2 texel = abs(pc.materialOptions.w) / viewportSize;
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
    if (pc.materialOptions.w >= 0.0) {
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
    if (pc.gridColor.a <= 0.0 || pc.shadowOptions.w <= 0.0 || pc.materialOptions.y <= 0.0 || pc.materialOptions.z <= 0.0) {
        return 0.0;
    }

    vec2 faceCoord = vec2(inFaceCoordU, inFaceCoordV);
    vec2 wrapped = fract(faceCoord);
    vec2 distanceToLine = min(wrapped, 1.0 - wrapped);
    vec2 coordPerPixel = max(fwidth(faceCoord), vec2(0.00001));
    vec2 halfWidth = coordPerPixel * pc.shadowOptions.w * 0.5;
    vec2 feather = coordPerPixel;
    vec2 line = 1.0 - smoothstep(halfWidth, halfWidth + feather, distanceToLine);
    return max(line.x, line.y) * pc.gridColor.a;
}

float shadowFactor(vec4 shadowPosition, float diffuse)
{
    if (pc.shadowOptions.x <= 0.5 || diffuse <= 0.0 || abs(shadowPosition.w) <= 0.0001) {
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
            shadowedSamples += shadowCoord.z - pc.shadowOptions.z > closestDepth ? 1.0 : 0.0;
        }
    }

    float shadowAmount = shadowedSamples / 9.0;
    return 1.0 - shadowAmount * pc.shadowOptions.y;
}

void main()
{
    applyEditorPreviewDither();

    vec4 materialColor = pc.color;
    int materialMode = int(pc.textureOptions.x + 0.5);
    if (materialMode == 2) {
        if (inTextureIndex > 1.5) {
            // materialOptions.y carries the belt scroll offset; the belt UVs span
            // a full 0..1 along V, so fract() wraps the scroll seamlessly.
            int textureIndex = clamp(int(inTextureIndex + pc.materialOptions.z + 0.5), 0, MODEL_TEXTURE_COUNT - 1);
            materialColor *= texture(modelTextures[textureIndex], vec2(inFaceCoordU, fract(inFaceCoordV + pc.materialOptions.y)));
        } else if (inTextureIndex > 0.5) {
            int textureIndex = clamp(int(inTextureIndex + pc.materialOptions.z + 0.5), 0, MODEL_TEXTURE_COUNT - 1);
            materialColor *= texture(modelTextures[textureIndex], vec2(inFaceCoordU, inFaceCoordV));
        }
    } else if (materialMode == 1) {
        int textureIndex = clamp(int(pc.materialOptions.z + 0.5), 0, MODEL_TEXTURE_COUNT - 1);
        materialColor *= texture(modelTextures[textureIndex], vec2(inFaceCoordU, inFaceCoordV));
    }
    vec3 color = mix(materialColor.rgb, pc.gridColor.rgb, gridMask());
    if (length(inNormal) > 0.0001) {
        vec3 normal = normalize(inNormal);
        vec3 lightDirection = length(pc.sunDirectionAndAmbientGreen.xyz) > 0.0001
            ? normalize(pc.sunDirectionAndAmbientGreen.xyz)
            : vec3(0.0, 0.0, 1.0);
        float rawDiffuse = dot(normal, lightDirection);
        float lambertDiffuse = max(rawDiffuse, 0.0);
        float wrappedDiffuse = clamp(rawDiffuse * 0.5 + 0.5, 0.0, 1.0);
        float diffuse = mix(lambertDiffuse, wrappedDiffuse * wrappedDiffuse, 0.65);
        vec3 ambient = vec3(
            pc.normalAndAmbientRed.w,
            pc.sunDirectionAndAmbientGreen.w,
            pc.sunRadianceAndAmbientBlue.w);
        float shadow = shadowFactor(inShadowPosition, lambertDiffuse);
        float skyFill = smoothstep(-0.35, 1.0, normal.z);
        vec3 diffuseLighting = ambient * (1.0 + skyFill * 0.35) + pc.sunRadianceAndAmbientBlue.rgb * diffuse * shadow;
        color *= diffuseLighting;

        float specularStrength = max(pc.textureOptions.z, 0.0);
        if (specularStrength > 0.0 && lambertDiffuse > 0.0) {
            const vec3 viewDirection = normalize(vec3(0.0, 0.25881904, 0.9659258));
            vec3 halfDirection = normalize(lightDirection + viewDirection);
            float specularPower = max(pc.textureOptions.w, 1.0);
            float specular = pow(max(dot(normal, halfDirection), 0.0), specularPower);
            specular *= smoothstep(0.0, 0.2, lambertDiffuse) * shadow * specularStrength;
            color += pc.sunRadianceAndAmbientBlue.rgb * specular * materialColor.a;
        }
    }

    if (pc.materialOptions.x > 0.5) {
        vec2 uv = gl_FragCoord.xy / vec2(textureSize(sceneColor, 0));
        vec3 blurred = gaussianBlurredScene(uv);
        outColor = vec4(mix(blurred, color, materialColor.a), 1.0);
        return;
    }

    outColor = vec4(color, materialColor.a);
}
