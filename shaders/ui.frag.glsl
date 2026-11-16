#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// Player-facing UI is composed into the display image after tonemapping. This
// fragment path deliberately knows nothing about scene lighting, shadows or
// materials; it only consumes the resources used by UiDrawCommand kinds.
layout(set = 0, binding = 1) uniform sampler2D sceneColor;
layout(set = 1, binding = 0) uniform sampler2D modelTextures[];
layout(set = 0, binding = 3) uniform sampler2D uiFont;
layout(set = 0, binding = 4) uniform sampler2D titleBackground;

layout(location = 1) in float inFaceCoordU;
layout(location = 2) in float inFaceCoordV;
layout(location = 7) flat in uint inDrawInstance;
layout(location = 0) out vec4 outColor;

#include "DrawInstance.glsl"
#include "NineSlice.glsl"

#define HB_GPU_ATLAS_2D
#include "HbGpu.glsl"

#define draw drawInstances.instances[inDrawInstance]

#include "DrawMode.glsl"

float roundedRectDistance(vec2 localPosition, vec2 rectSize, float cornerRadius)
{
    cornerRadius = clamp(cornerRadius, 0.0, min(rectSize.x, rectSize.y) * 0.5);
    vec2 centeredPosition = (localPosition - 0.5) * rectSize;
    vec2 distanceToCorner = abs(centeredPosition) - (rectSize * 0.5 - cornerRadius);
    return length(max(distanceToCorner, vec2(0.0))) +
        min(max(distanceToCorner.x, distanceToCorner.y), 0.0) - cornerRadius;
}

float sceneImageCutoutOpacity()
{
    // A signed-distance rounded rectangle makes the preview's outside edge
    // genuinely transparent and gives all corners one continuous curve.
    vec2 uvSpan = max(draw.materialOptions.yz, vec2(0.00001));
    vec2 localPosition = vec2(inFaceCoordU, inFaceCoordV) / uvSpan;
    vec2 rectSize = max(draw.shadowOptions.xy, vec2(1.0));
    float featherWidth = max(draw.shadowOptions.z, 0.001);
    float signedDistance = roundedRectDistance(localPosition, rectSize, draw.shadowOptions.w);
    float normalizedDistance = clamp(
        -signedDistance / featherWidth,
        0.0,
        1.0);
    float endpointSoftened = smoothstep(0.0, 1.0, normalizedDistance);

    // This normalized logarithmic remap is the existing scene-preview edge
    // profile. Keep its pixels stable while moving it out of the lit shader.
    const float logarithmicStrength = 2.0;
    return log(1.0 + logarithmicStrength * endpointSoftened) /
        log(1.0 + logarithmicStrength);
}

vec3 colorGradient(vec2 uv)
{
    vec3 top = mix(draw.passData[0].rgb, draw.passData[1].rgb, uv.x);
    vec3 bottom = mix(draw.passData[2].rgb, draw.passData[3].rgb, uv.x);
    vec3 srgb = clamp(mix(top, bottom, uv.y), 0.0, 1.0);
    return mix(pow((srgb + 0.055) / 1.055, vec3(2.4)), srgb / 12.92,
        lessThanEqual(srgb, vec3(0.04045)));
}

void main()
{
    vec4 color = draw.color;
    vec2 uv = draw.gridColor.xy + vec2(inFaceCoordU, inFaceCoordV);
    int mode = int(draw.textureOptions.x + 0.5);

    if (mode == DRAW_MODE_FONT_GLYPH) {
        color.a *= draw.textureOptions.z > 0.5
            ? _hb_gpu_slug(uv, 1.0 / max(fwidth(uv), vec2(0.00001)), uint(draw.textureOptions.y + 0.5))
            : texture(uiFont, uv).r;
    } else if (mode == DRAW_MODE_TITLE_BACKGROUND) {
        color *= texture(titleBackground, uv);
    } else if (mode == DRAW_MODE_SCENE_IMAGE) {
        // Scene alpha carries the ambient-light ratio, not opacity.
        color.rgb *= texture(sceneColor, uv).rgb;
        color.a *= 1.0 - sceneImageCutoutOpacity();
    } else if (mode == DRAW_MODE_TEXTURE_IMAGE) {
        int textureIndex = max(int(draw.textureOptions.y + 0.5) - 1, 0);
        vec2 uvSpan = max(draw.materialOptions.yz, vec2(0.00001));
        vec2 localUv = vec2(inFaceCoordU, inFaceCoordV) / uvSpan;
        localUv = nineSliceUv(
            localUv,
            draw.passData[0],
            draw.passData[1].xy,
            vec2(textureSize(
                modelTextures[nonuniformEXT(textureIndex)], 0)) * uvSpan,
            draw.passData[1].z);
        uv = draw.gridColor.xy + localUv * uvSpan;
        color *= texture(
            modelTextures[nonuniformEXT(textureIndex)], uv);
    } else if (mode == DRAW_MODE_UI_COLOR_GRADIENT) {
        color = vec4(colorGradient(uv), 1.0);
    } else if (mode != DRAW_MODE_UNTEXTURED) {
        // UiDrawKind is a closed CPU enum. Make a future unmapped kind
        // conspicuous instead of accidentally rendering it as a solid quad.
        color = vec4(1.0, 0.0, 1.0, color.a);
    }

    if ((mode == DRAW_MODE_TEXTURE_IMAGE || mode == DRAW_MODE_UNTEXTURED) && draw.passData[1].w > 0.0) {
        vec2 uvSpan = max(draw.materialOptions.yz, vec2(0.00001));
        vec2 localPosition = vec2(inFaceCoordU, inFaceCoordV) / uvSpan;
        float distance = roundedRectDistance(localPosition, draw.passData[1].xy, draw.passData[1].w);
        float pixelWidth = max(fwidth(distance), 0.001);
        color.a *= clamp(0.5 - distance / pixelWidth, 0.0, 1.0);
    }

    outColor = color;
}
