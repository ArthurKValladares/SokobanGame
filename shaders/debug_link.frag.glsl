#version 460
#extension GL_GOOGLE_include_directive : require
layout(location = 1) in float inFaceCoordU;
layout(location = 2) in float inFaceCoordV;
layout(location = 7) flat in uint inDrawInstance;
layout(location = 0) out vec4 outColor;
#include "DrawInstance.glsl"
#define draw drawInstances.instances[inDrawInstance]
float segmentDistance(vec2 point, vec2 a, vec2 b)
{
    vec2 delta = b - a;
    return length(point - a - delta * clamp(dot(point - a, delta) / dot(delta, delta), 0.0, 1.0));
}
float arrowDistance(vec2 point, float direction)
{
    point.x *= direction;
    return min(segmentDistance(point, vec2(-4.0, -4.0), vec2(0.0)),
               segmentDistance(point, vec2(-4.0, 4.0), vec2(0.0))) - 1.4;
}
void main()
{
    // UV is measured in screen pixels, so dots remain round and evenly spaced
    // across layer changes, perspective and render resolution changes.
    float spacing = draw.passData[0].x;
    float radius = draw.passData[0].y;
    float border = draw.passData[0].z;
    int style = int(draw.passData[0].w);
    vec2 point = vec2(inFaceCoordU, inFaceCoordV - draw.materialOptions.z * 0.5);
    float distance;
    if (style == 0) {
        point.x = mod(point.x + spacing * 0.5, spacing) - spacing * 0.5;
        distance = length(point) - radius;
    } else if (style == 4) {
        distance = length(point) - 5.0;
    } else {
        float length = draw.materialOptions.w;
        distance = max(abs(point.y) - 1.4, abs(point.x - length * 0.5) - length * 0.5);
        if (style == 1 || style == 2) {
            float phase = mod(point.x, spacing);
            float arrows = style == 1
                ? arrowDistance(vec2(phase - spacing * 0.5, point.y), 1.0)
                : min(arrowDistance(vec2(phase - spacing * 0.33, point.y), 1.0),
                      arrowDistance(vec2(phase - spacing * 0.67, point.y), -1.0));
            // Keep repeated arrows inside the authored segment's endpoints.
            distance = min(distance, max(arrows, max(-point.x, point.x - length)));
        }
    }
    float blackBacking = 1.0 - smoothstep(border - 0.5, border + 0.5, distance);
    float coloredShape = 1.0 - smoothstep(-0.5, 0.5, distance);
    if (blackBacking <= 0.0) { discard; }
    outColor = vec4(draw.color.rgb * coloredShape, draw.color.a * blackBacking);
}
