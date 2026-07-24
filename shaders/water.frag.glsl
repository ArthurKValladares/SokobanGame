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

void main()
{
    if (pc.materialOptions.w < 0.0) {
        ivec2 ditherPixel = ivec2(floor(gl_FragCoord.xy * 0.5));
        if (bayer8x8(ditherPixel) >= 0.56) {
            discard;
        }
    }

    vec2 worldPosition =
        pc.gridColor.xy + vec2(inFaceCoordU, inFaceCoordV);
    float frequency = max(pc.textureOptions.x, 0.01);
    float time = pc.gridColor.z * pc.textureOptions.y;

    const vec2 directionA = vec2(0.8192319, 0.5734623);
    const vec2 directionB = vec2(-0.4472136, 0.8944272);
    float phaseA = dot(worldPosition, directionA) * frequency + time;
    float phaseB =
        dot(worldPosition, directionB) * frequency * 1.47 - time * 1.31;

    float waveA = sin(phaseA);
    float waveB = sin(phaseB);
    float wave = waveA * 0.62 + waveB * 0.38;
    vec2 gradient =
        cos(phaseA) * directionA * 0.62 +
        cos(phaseB) * directionB * 0.56;

    vec2 sceneSize = vec2(textureSize(sceneColor, 0));
    vec2 sceneUv = gl_FragCoord.xy / sceneSize;
    vec2 refractionOffset =
        gradient * pc.textureOptions.z *
        vec2(sceneSize.y / max(sceneSize.x, 1.0), 1.0);
    vec3 refractedScene = texture(
        sceneColor,
        clamp(sceneUv + refractionOffset, vec2(0.001), vec2(0.999))).rgb;

    float normalizedWave = wave * 0.5 + 0.5;
    float antialiasWidth = max(fwidth(normalizedWave), 0.015);
    float crest = smoothstep(
        0.70 - antialiasWidth,
        0.82 + antialiasWidth,
        normalizedWave);
    float crossing = pow(max(waveA * waveB, 0.0), 4.0);

    vec3 tint = pc.color.rgb * mix(0.80, 1.08, normalizedWave);
    tint += vec3(0.20, 0.48, 0.58) * crest * 0.20;
    tint += vec3(0.42, 0.72, 0.80) * crossing * 0.12;
    float opacity = clamp(pc.color.a + crest * 0.06, 0.0, 0.92);

    outColor = vec4(mix(refractedScene, tint, opacity), 1.0);
}
