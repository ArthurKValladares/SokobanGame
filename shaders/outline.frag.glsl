#version 460
#extension GL_GOOGLE_include_directive : require
layout(location = 1) in float inFaceCoordU;
layout(location = 2) in float inFaceCoordV;
layout(location = 7) flat in uint inDrawInstance;
layout(location = 0) out vec4 outColor;
#include "DrawInstance.glsl"
#define draw drawInstances.instances[inDrawInstance]
void main()
{
    float coverage = 1.0;
    float mode = draw.passData[0].w;
    if (mode > 1.5 && mode < 2.5) {
        float width = max(fwidth(inFaceCoordU) * draw.passData[0].z, 0.00001);
        coverage = 1.0 - smoothstep(0.0, width, abs(inFaceCoordU));
        if (coverage <= 0.0) { discard; }
    } else if (mode > 0.5 && mode < 1.5) {
        vec2 uv = vec2(inFaceCoordU, inFaceCoordV);
        vec2 pixelsFromEdge = min(uv, 1.0 - uv) / max(fwidth(uv), vec2(0.00001));
        coverage = 1.0 - smoothstep(draw.passData[0].z - 0.5,
                                  draw.passData[0].z + 0.5,
                                  min(pixelsFromEdge.x, pixelsFromEdge.y));
        if (coverage <= 0.0) { discard; }
    }
    outColor = vec4(draw.color.rgb, draw.color.a * coverage);
}
