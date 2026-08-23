#version 460

// Ground splatting: blends two ground textures (a base and a detail layer)
// using the red channel of a splat map, then applies the same lighting,
// shadowing, grid overlay, and editor-preview dithering as the standard tile
// shader so splatted ground sits seamlessly beside every other surface.
//
// Material UVs are world-grid based (one texture repeat spans
// GROUND_UV_TILES tiles), so the pattern is continuous across adjacent tiles
// and stable while the camera moves. Splat UVs are region-local so each
// composed-overworld screen can own an independent paint map.

layout(set = 0, binding = 0) uniform sampler2D shadowMap;
layout(set = 0, binding = 2) uniform sampler2D modelTextures[MODEL_TEXTURE_COUNT];
layout(set = 0, binding = 8) uniform samplerCubeArray pointShadowMaps;

layout(location = 0) in vec4 inShadowPosition;
layout(location = 1) in float inFaceCoordU;
layout(location = 2) in float inFaceCoordV;
layout(location = 3) in vec3 inNormal;
layout(location = 6) in vec3 inWorldPosition;
layout(location = 0) out vec4 outColor;

struct PointLightData
{
    vec4 positionAndRange;
    vec4 colorAndIntensity;
    vec4 shadowOptions;
};
layout(std140, set = 0, binding = 7) uniform SceneLighting
{
    vec4 sunShadowRightAndHalfWidth;
    vec4 sunShadowUpAndHalfHeight;
    vec4 sunShadowForwardAndDepthRange;
    vec4 sunShadowCenterAndNearestDepth;
    PointLightData pointLights[8];
    vec4 pointLightMeta;
} lighting;

layout(push_constant) uniform PushConstants
{
    vec4 vertices[4];
    vec4 shadowVertices[4];
    vec4 color;
    vec4 normalAndAmbientRed;
    vec4 sunDirectionAndAmbientGreen;
    vec4 sunRadianceAndAmbientBlue;
    vec4 shadowOptions;
    // x: splat-region-local origin X, y/z: face size in tiles,
    // w: editor-preview dither sign
    vec4 materialOptions;
    vec4 gridColor;
    // Texture handles are one-based; 0 means "unresolved", which falls back to
    // the flat tile color. The standard tile path leaves this vector free on
    // ground faces, so the splat pass repurposes it without growing the
    // 256-byte push-constant block.
    // x: base texture, y: detail texture, z: splat map,
    // w: splat-region-local origin Y.
    // (UV tile span is a shader constant below.)
    vec4 textureOptions;
} pc;

// Local origin of this face inside its splat region: X rides in the blur slot
// (opaque ground never blurs) and Y in the last texture slot.
#define SPLAT_LOCAL_ORIGIN (vec2(pc.materialOptions.x, pc.textureOptions.w))

// One texture repeat spans this many board tiles. Larger = coarser detail.
// This applies to the tiling grass/rock material layers only.
const float GROUND_UV_TILES = 4.0;

// The splat map does NOT tile: one map covers one screen's board exactly, so
// that painting a spot in the editor affects only that spot. Its coverage is
// derived from its own dimensions rather than pushed per face, because the
// 256-byte push-constant block is full - maps are authored at exactly
// GROUND_SPLAT_TEXELS_PER_TILE texels per board tile, so
// textureSize / texelsPerTile is the board size in tiles. Changing this
// constant means regenerating every map (tools/make_ground_textures.py).
const float GROUND_SPLAT_TEXELS_PER_TILE = 32.0;

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

    vec3 projected = shadowPosition.xyz / shadowPosition.w;
    if (projected.z <= 0.0 || projected.z >= 1.0) {
        return 1.0;
    }

    vec2 shadowUv = projected.xy * 0.5 + 0.5;
    if (any(lessThan(shadowUv, vec2(0.0))) || any(greaterThan(shadowUv, vec2(1.0)))) {
        return 1.0;
    }

    float bias = pc.shadowOptions.z;
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float shadowedSamples = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float depth = texture(shadowMap, shadowUv + vec2(float(x), float(y)) * texel).r;
            shadowedSamples += projected.z - bias > depth ? 1.0 : 0.0;
        }
    }

    float shadowAmount = shadowedSamples / 9.0;
    return 1.0 - shadowAmount * pc.shadowOptions.y;
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
    PointLightData light = lighting.pointLights[lightIndex];
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
    vec3 direction = normalize(fromLight);
    // The authored bias is a world-space minimum. Increase it at grazing
    // angles, where rasterized depth changes fastest across the surface.
    float facing = clamp(dot(normalize(surfaceNormal), -direction), 0.0, 1.0);
    float worldBias = max(light.shadowOptions.z, 0.0) *
        (1.0 + 2.0 * (1.0 - facing));
    float closestDepth = texture(
        pointShadowMaps,
        vec4(direction, light.shadowOptions.y)).r;
    float closestDistance = pointShadowWorldDistance(
        closestDepth, nearPlane, farPlane);
    float shadowed = majorDistance - worldBias > closestDistance
        ? 1.0
        : 0.0;
    return 1.0 - shadowed * clamp(light.shadowOptions.w, 0.0, 1.0);
}

// One-based handle -> texture array index; returns false when unresolved.
bool resolveTexture(float handle, out int index)
{
    if (handle < 0.5) {
        return false;
    }
    index = clamp(int(handle - 1.0 + 0.5), 0, MODEL_TEXTURE_COUNT - 1);
    return true;
}

void main()
{
    applyEditorPreviewDither();

    // Face-local coords span the face's size in tiles. The recorder supplies
    // a region-local origin for splat lookup, while the actual face vertices
    // retain global coordinates for continuous material-layer tiling.
    vec2 faceTiles = vec2(
        max(pc.materialOptions.y, 0.0001),
        max(pc.materialOptions.z, 0.0001));
    vec2 faceCoord = vec2(inFaceCoordU, inFaceCoordV) * faceTiles;
    vec2 splatLocalTile = SPLAT_LOCAL_ORIGIN + faceCoord;
    vec2 globalOrigin = min(
        min(pc.vertices[0].xy, pc.vertices[1].xy),
        min(pc.vertices[2].xy, pc.vertices[3].xy));
    vec2 worldTile = globalOrigin + faceCoord;
    vec2 uv = worldTile / GROUND_UV_TILES;

    vec4 materialColor = pc.color;
    int baseIndex = 0;
    int detailIndex = 0;
    int splatIndex = 0;
    if (resolveTexture(pc.textureOptions.x, baseIndex)) {
        vec3 baseColor = texture(modelTextures[baseIndex], uv).rgb;
        vec3 blended = baseColor;
        if (resolveTexture(pc.textureOptions.y, detailIndex)) {
            vec3 detailColor = texture(modelTextures[detailIndex], uv).rgb;
            // The splat map spans the board once. Its size tells us how many
            // tiles that is, so world tile -> 0..1 needs nothing pushed.
            float weight = 0.0;
            if (resolveTexture(pc.textureOptions.z, splatIndex)) {
                vec2 splatBoardTiles = max(
                    vec2(textureSize(modelTextures[splatIndex], 0)) /
                        GROUND_SPLAT_TEXELS_PER_TILE,
                    vec2(1.0));
                // Clamped, not wrapped: ground outside the board (the
                // continuation skirt) holds the edge weight instead of
                // repeating the board's pattern back over itself.
                vec2 splatUv = clamp(
                    splatLocalTile / splatBoardTiles, 0.0, 1.0);
                weight = texture(modelTextures[splatIndex], splatUv).r;
            }
            blended = mix(baseColor, detailColor, clamp(weight, 0.0, 1.0));
        }
        // Modulate by the tile color so gameplay tinting (active plates,
        // editor highlights) still reads through the texture.
        materialColor.rgb *= blended;
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
        vec3 pointDiffuseLighting = vec3(0.0);
        int pointLightCount = clamp(int(lighting.pointLightMeta.x + 0.5), 0, 8);
        for (int lightIndex = 0; lightIndex < pointLightCount; ++lightIndex) {
            PointLightData pointLight = lighting.pointLights[lightIndex];
            vec3 toLight = pointLight.positionAndRange.xyz - inWorldPosition;
            float distanceToLight = length(toLight);
            float range = max(pointLight.positionAndRange.w, 0.001);
            if (distanceToLight <= 0.0001 || distanceToLight >= range) {
                continue;
            }
            float pointLambert = max(
                dot(normal, toLight / distanceToLight), 0.0);
            float normalizedDistance = distanceToLight / range;
            float rangeWindow = max(
                1.0 - pow(normalizedDistance, 4.0), 0.0);
            float attenuation = rangeWindow * rangeWindow /
                max(distanceToLight * distanceToLight, 0.04);
            pointDiffuseLighting += pointLight.colorAndIntensity.rgb *
                pointLight.colorAndIntensity.w * attenuation *
                pointLambert * pointShadowFactor(
                    lightIndex, -toLight, normal);
        }
        vec3 diffuseLighting = ambient * (1.0 + skyFill * 0.35) +
            pc.sunRadianceAndAmbientBlue.rgb * diffuse * shadow +
            pointDiffuseLighting;
        color *= diffuseLighting;
    }

    outColor = vec4(color, materialColor.a);
}
