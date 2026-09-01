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

#define draw drawInstances.instances[inDrawInstance]

const int UI_MODE_SOLID = 0;
const int UI_MODE_FONT_GLYPH = 3;
const int UI_MODE_TITLE_BACKGROUND = 4;
const int UI_MODE_SCENE_IMAGE = 6;
const int UI_MODE_TEXTURE_IMAGE = 7;

float sceneImageCutoutOpacity()
{
    // A signed-distance rounded rectangle makes the preview's outside edge
    // genuinely transparent and gives all corners one continuous curve.
    vec2 uvSpan = max(draw.materialOptions.yz, vec2(0.00001));
    vec2 localPosition = vec2(inFaceCoordU, inFaceCoordV) / uvSpan;
    vec2 rectSize = max(draw.shadowOptions.xy, vec2(1.0));
    float featherWidth = max(draw.shadowOptions.z, 0.001);
    float cornerRadius = clamp(
        draw.shadowOptions.w,
        0.0,
        min(rectSize.x, rectSize.y) * 0.5);
    vec2 centeredPosition = localPosition * rectSize - rectSize * 0.5;
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
    float endpointSoftened = smoothstep(0.0, 1.0, normalizedDistance);

    // This normalized logarithmic remap is the existing scene-preview edge
    // profile. Keep its pixels stable while moving it out of the lit shader.
    const float logarithmicStrength = 2.0;
    return log(1.0 + logarithmicStrength * endpointSoftened) /
        log(1.0 + logarithmicStrength);
}

void main()
{
    vec4 color = draw.color;
    vec2 uv = draw.gridColor.xy + vec2(inFaceCoordU, inFaceCoordV);
    int mode = int(draw.textureOptions.x + 0.5);

    if (mode == UI_MODE_FONT_GLYPH) {
        color.a *= texture(uiFont, uv).r;
    } else if (mode == UI_MODE_TITLE_BACKGROUND) {
        color *= texture(titleBackground, uv);
    } else if (mode == UI_MODE_SCENE_IMAGE) {
        // Scene alpha carries the ambient-light ratio, not opacity.
        color.rgb *= texture(sceneColor, uv).rgb;
        color.a *= 1.0 - sceneImageCutoutOpacity();
    } else if (mode == UI_MODE_TEXTURE_IMAGE) {
        int textureIndex = max(int(draw.textureOptions.y + 0.5) - 1, 0);
        color *= texture(
            modelTextures[nonuniformEXT(textureIndex)], uv);
    } else if (mode != UI_MODE_SOLID) {
        // UiDrawKind is a closed CPU enum. Make a future unmapped kind
        // conspicuous instead of accidentally rendering it as a solid quad.
        color = vec4(1.0, 0.0, 1.0, color.a);
    }

    outColor = color;
}
