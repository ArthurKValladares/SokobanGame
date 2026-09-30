#ifndef SOKOBAN_NINE_SLICE_GLSL
#define SOKOBAN_NINE_SLICE_GLSL

// GPU half of engine/render/NineSlice.hpp. The source border is normalized
// and ordered left, top, right, bottom. sourcePixelScale is target units per
// source texel; zero derives a uniform scale by fitting the source width to
// the target width, which is useful for world-space ribbons.

vec2 fitNineSliceBorderPair(vec2 borders)
{
    float total = borders.x + borders.y;
    return total > 0.998 ? borders * (0.998 / total) : borders;
}

float nineSliceCoordinate(
    float coordinate,
    float sourceLow,
    float sourceHigh,
    float targetLow,
    float targetHigh)
{
    if (coordinate < targetLow && targetLow > 0.000001) {
        return coordinate * sourceLow / targetLow;
    }
    if (coordinate > 1.0 - targetHigh && targetHigh > 0.000001) {
        return 1.0 - (1.0 - coordinate) * sourceHigh / targetHigh;
    }
    float targetCenter = max(1.0 - targetLow - targetHigh, 0.000001);
    float sourceCenter = max(1.0 - sourceLow - sourceHigh, 0.0);
    return sourceLow +
        (coordinate - targetLow) * sourceCenter / targetCenter;
}

vec2 nineSliceUv(
    vec2 uv,
    vec4 sourceBorders,
    vec2 targetExtent,
    vec2 sourceExtent,
    float sourcePixelScale)
{
    sourceBorders = clamp(sourceBorders, vec4(0.0), vec4(0.499));
    if (!any(greaterThan(sourceBorders, vec4(0.0)))) {
        return uv;
    }

    targetExtent = max(targetExtent, vec2(0.0001));
    sourceExtent = max(sourceExtent, vec2(1.0));
    float scale = sourcePixelScale > 0.0
        ? sourcePixelScale
        : targetExtent.x / sourceExtent.x;

    vec2 targetHorizontal = fitNineSliceBorderPair(
        vec2(sourceBorders.x, sourceBorders.z) *
        sourceExtent.x * scale / targetExtent.x);
    vec2 targetVertical = fitNineSliceBorderPair(
        vec2(sourceBorders.y, sourceBorders.w) *
        sourceExtent.y * scale / targetExtent.y);

    return vec2(
        nineSliceCoordinate(
            uv.x,
            sourceBorders.x,
            sourceBorders.z,
            targetHorizontal.x,
            targetHorizontal.y),
        nineSliceCoordinate(
            uv.y,
            sourceBorders.y,
            sourceBorders.w,
            targetVertical.x,
            targetVertical.y));
}

#endif
