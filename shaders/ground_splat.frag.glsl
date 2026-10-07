#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

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
layout(set = 1, binding = 0) uniform sampler2D modelTextures[];
layout(set = 0, binding = 8) uniform samplerCubeArray pointShadowMaps;

layout(location = 0) in vec4 inShadowPosition;
layout(location = 1) in float inFaceCoordU;
layout(location = 2) in float inFaceCoordV;
layout(location = 3) in vec3 inNormal;
layout(location = 6) in vec3 inWorldPosition;
layout(location = 7) flat in uint inDrawInstance;
layout(location = 0) out vec4 outColor;

#include "AmbientMask.glsl"

#include "DrawInstance.glsl"

#define draw drawInstances.instances[inDrawInstance]

#include "SceneFrame.glsl"

// The splat region's world origin is carried in draw.passData[1].yz. Using
// world positions keeps both paint and tiled materials continuous across rim
// subpatches.

// One texture repeat spans this many board tiles. Larger = coarser detail.
// This applies to the tiling grass/rock material layers only.
const float GROUND_UV_TILES = 4.0;

// The splat map does NOT tile: one map covers one screen's board exactly, so
// that painting a spot in the editor affects only that spot. Its coverage is
// derived from its own dimensions: maps are authored at exactly
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

    vec2 faceCoord = inWorldPosition.xy;
    vec2 wrapped = fract(faceCoord);
    vec2 distanceToLine = min(wrapped, 1.0 - wrapped);
    vec2 coordPerPixel = max(fwidth(faceCoord), vec2(0.00001));
    vec2 halfWidth = coordPerPixel * draw.shadowOptions.w * 0.5;
    vec2 feather = coordPerPixel;
    vec2 line = 1.0 - smoothstep(halfWidth, halfWidth + feather, distanceToLine);
    return max(line.x, line.y) * draw.gridColor.a;
}

#include "DirectionalShadow.glsl"

#define POINT_SHADOW_TAPS 1
#include "PointShadow.glsl"
#include "PbrLighting.glsl"

// One-based handle -> texture array index; returns false when unresolved.
bool resolveTexture(float handle, out int index)
{
    if (handle < 0.5) {
        return false;
    }
    index = max(int(handle - 1.0 + 0.5), 0);
    return true;
}

vec3 sampleGroundData(float handle, vec2 uv, vec3 fallback)
{
    int index = 0;
    return resolveTexture(handle, index)
        ? texture(modelTextures[nonuniformEXT(index)], uv).rgb
        : fallback;
}

void main()
{
    applyEditorPreviewDither();

    // Paint lookup is region-local; material layers use the global grid.
    vec2 splatLocalTile = inWorldPosition.xy - draw.passData[1].yz;
    vec2 worldTile = inWorldPosition.xy;
    vec2 uv = worldTile / GROUND_UV_TILES;

    vec4 materialColor = draw.color;
    float weight = 0.0;
    int baseIndex = 0;
    int detailIndex = 0;
    int splatIndex = 0;
    if (resolveTexture(draw.textureOptions.x, baseIndex)) {
        vec3 baseColor = texture(
            modelTextures[nonuniformEXT(baseIndex)], uv).rgb;
        vec3 blended = baseColor;
        if (resolveTexture(draw.textureOptions.y, detailIndex)) {
            vec3 detailColor = texture(
                modelTextures[nonuniformEXT(detailIndex)], uv).rgb;
            // The splat map spans the board once. Its size tells us how many
            // tiles that is, so world tile -> 0..1 needs nothing pushed.
            if (resolveTexture(draw.textureOptions.z, splatIndex)) {
                vec2 splatBoardTiles = max(
                    vec2(textureSize(
                        modelTextures[nonuniformEXT(splatIndex)], 0)) /
                        GROUND_SPLAT_TEXELS_PER_TILE,
                    vec2(1.0));
                // Clamped, not wrapped: ground outside the board (the
                // continuation skirt) holds the edge weight instead of
                // repeating the board's pattern back over itself.
                vec2 splatUv = clamp(
                    splatLocalTile / splatBoardTiles, 0.0, 1.0);
                weight = texture(
                    modelTextures[nonuniformEXT(splatIndex)], splatUv).r;
            }
            blended = mix(baseColor, detailColor, clamp(weight, 0.0, 1.0));
        }
        // Modulate by the tile color so gameplay tinting (active plates,
        // editor highlights) still reads through the texture.
        materialColor.rgb *= blended;
    }

    // Data maps use the exact same world UVs and splat weights as albedo.
    // Unregistered companion maps retain a neutral normal and matte response.
    vec3 baseNormal = sampleGroundData(draw.passData[0].x, uv, vec3(.5, .5, 1.0)) * 2.0 - 1.0;
    vec3 detailNormal = sampleGroundData(draw.passData[0].y, uv, vec3(.5, .5, 1.0)) * 2.0 - 1.0;
    vec3 tangentNormal = normalize(mix(baseNormal, detailNormal, clamp(weight, 0.0, 1.0)));
    vec3 orm = mix(
        sampleGroundData(draw.passData[0].z, uv, vec3(1.0, 1.0, 0.0)),
        sampleGroundData(draw.passData[0].w, uv, vec3(1.0, 1.0, 0.0)),
        clamp(weight, 0.0, 1.0));
    vec3 color = mix(materialColor.rgb, draw.gridColor.rgb, gridMask());
    float ambientMask = 0.0;
    if (length(inNormal) > 0.0001) {
        vec3 geometricNormal = normalize(inNormal);
        // Ground UVs increase in world X and Y. Rebuild the frame for the
        // supplied geometric normal instead of using the quad's default tangent.
        vec3 tangent = normalize(vec3(1.0, 0.0, 0.0) -
            geometricNormal * geometricNormal.x);
        vec3 bitangent = cross(geometricNormal, tangent);
        vec3 normal = normalize(tangent * tangentNormal.x +
            bitangent * tangentNormal.y + geometricNormal * tangentNormal.z);
        vec3 lightDirection = length(draw.sunDirectionAndAmbientGreen.xyz) > 0.0001
            ? normalize(draw.sunDirectionAndAmbientGreen.xyz)
            : vec3(0.0, 0.0, 1.0);
        vec3 ambient = vec3(
            draw.normalAndAmbientRed.w,
            draw.sunDirectionAndAmbientGreen.w,
            draw.sunRadianceAndAmbientBlue.w);
        float shadow = shadowFactor(inShadowPosition,
            max(dot(geometricNormal, lightDirection), 0.0));
        float skyFill = smoothstep(-0.35, 1.0, normal.z);
        float metallic = clamp(orm.b, 0.0, 1.0);
        float roughness = clamp(orm.g, 0.045, 1.0);
        vec3 f0 = mix(vec3(0.04), color, metallic);
        vec3 diffuseAlbedo = color * (1.0 - metallic);
        vec3 viewDirection = normalize(frame.cameraPositionAndNearPlane.xyz - inWorldPosition);
        Shaded sun = shadeLight(normal, viewDirection, lightDirection,
            draw.sunRadianceAndAmbientBlue.rgb * shadow, diffuseAlbedo, f0, roughness);
        vec3 diffuseLight = sun.diffuse;
        vec3 specularLight = sun.specular;
        int pointLightCount = clamp(int(frame.pointLightMeta.x + 0.5), 0, 8);
        for (int lightIndex = 0; lightIndex < pointLightCount; ++lightIndex) {
            PointLightData pointLight = frame.pointLights[lightIndex];
            vec3 toLight = pointLight.positionAndRange.xyz - inWorldPosition;
            float distanceSquared = dot(toLight, toLight);
            float normalizedDistanceSquared = distanceSquared *
                pointLight.radianceAndInverseRangeSquared.w;
            if (distanceSquared <= 0.00000001 || normalizedDistanceSquared >= 1.0) continue;
            vec3 pointDirection = toLight * inversesqrt(distanceSquared);
            if (dot(normal, pointDirection) <= 0.0) continue;
            float rangeWindow = 1.0 - normalizedDistanceSquared * normalizedDistanceSquared;
            float attenuation = rangeWindow * rangeWindow / max(distanceSquared, 0.04);
            vec3 radiance = pointLight.radianceAndInverseRangeSquared.rgb * attenuation *
                pointShadowFactorPrepared(pointLight, -toLight, -pointDirection, geometricNormal);
            Shaded point = shadeLight(normal, viewDirection, pointDirection,
                radiance, diffuseAlbedo, f0, roughness);
            diffuseLight += point.diffuse;
            specularLight += point.specular;
        }
        // Cavity AO and the scene's SSAO affect ambient only, as on model materials.
        vec3 ambientContribution = ambient * (1.0 + skyFill * 0.35) *
            (diffuseAlbedo + f0) * clamp(orm.r, 0.0, 1.0);
        color = diffuseLight + ambientContribution +
            specularLight * max(draw.passData[1].x, 0.0);
        ambientMask = clamp(dot(ambientContribution, luminanceWeights) /
            max(dot(color, luminanceWeights), 0.0001), 0.0, 1.0);
    }

    outColor = vec4(
        color, writeAmbientMask ? ambientMask : materialColor.a);
}
