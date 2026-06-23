#version 460

layout(set = 0, binding = 0) uniform sampler2D shadowMap;
layout(set = 0, binding = 1) uniform sampler2D sceneColor;

layout(location = 0) in vec4 inShadowPosition;
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
} pc;

vec3 gaussianBlurredScene(vec2 uv)
{
    const float weights[5] = float[5](1.0, 4.0, 6.0, 4.0, 1.0);
    vec2 viewportSize = vec2(textureSize(sceneColor, 0));
    vec2 texel = pc.materialOptions.w / viewportSize;
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

void main()
{
    vec3 color = mix(pc.color.rgb, pc.gridColor.rgb, gridMask());
    if (length(pc.normalAndAmbientRed.xyz) > 0.0001) {
        vec3 normal = normalize(pc.normalAndAmbientRed.xyz);
        vec3 lightDirection = length(pc.sunDirectionAndAmbientGreen.xyz) > 0.0001
            ? normalize(pc.sunDirectionAndAmbientGreen.xyz)
            : vec3(0.0, 0.0, 1.0);
        float diffuse = max(dot(normal, lightDirection), 0.0);
        vec3 ambient = vec3(
            pc.normalAndAmbientRed.w,
            pc.sunDirectionAndAmbientGreen.w,
            pc.sunRadianceAndAmbientBlue.w);
        float shadowFactor = 1.0;
        if (pc.shadowOptions.x > 0.5 && diffuse > 0.0 && abs(inShadowPosition.w) > 0.0001) {
            vec3 shadowCoord = inShadowPosition.xyz / inShadowPosition.w;
            vec2 shadowUv = shadowCoord.xy * 0.5 + 0.5;
            if (all(greaterThanEqual(shadowUv, vec2(0.0))) &&
                all(lessThanEqual(shadowUv, vec2(1.0))) &&
                shadowCoord.z >= 0.0 &&
                shadowCoord.z <= 1.0) {
                float closestDepth = texture(shadowMap, shadowUv).r;
                if (shadowCoord.z - pc.shadowOptions.z > closestDepth) {
                    shadowFactor = 1.0 - pc.shadowOptions.y;
                }
            }
        }
        color *= ambient + pc.sunRadianceAndAmbientBlue.rgb * diffuse * shadowFactor;
    }

    if (pc.materialOptions.x > 0.5) {
        vec2 uv = gl_FragCoord.xy / vec2(textureSize(sceneColor, 0));
        vec3 blurred = gaussianBlurredScene(uv);
        outColor = vec4(mix(blurred, color, pc.color.a), 1.0);
        return;
    }

    outColor = vec4(color, pc.color.a);
}
