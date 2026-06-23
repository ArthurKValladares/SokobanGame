#version 460

layout(set = 0, binding = 0) uniform sampler2D shadowMap;
layout(set = 0, binding = 1) uniform sampler2D sceneColor;

layout(location = 0) in vec4 inShadowPosition;
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

vec2 clipToScreen(vec4 clipPosition, vec2 viewportSize)
{
    vec2 ndc = clipPosition.xy / clipPosition.w;
    return vec2(
        (ndc.x * 0.5 + 0.5) * viewportSize.x,
        (1.0 - (ndc.y * 0.5 + 0.5)) * viewportSize.y);
}

float distanceToSegment(vec2 point, vec2 start, vec2 end)
{
    vec2 segment = end - start;
    float segmentLengthSquared = max(dot(segment, segment), 0.00001);
    float t = clamp(dot(point - start, segment) / segmentLengthSquared, 0.0, 1.0);
    return length(point - (start + segment * t));
}

float lineMask(vec2 point, vec2 start, vec2 end, float halfWidth)
{
    float distanceToLine = distanceToSegment(point, start, end);
    return 1.0 - smoothstep(halfWidth, halfWidth + 1.0, distanceToLine);
}

float gridMask()
{
    if (pc.gridColor.a <= 0.0 || pc.shadowOptions.w <= 0.0 || pc.materialOptions.y <= 0.0 || pc.materialOptions.z <= 0.0) {
        return 0.0;
    }

    vec2 viewportSize = vec2(textureSize(sceneColor, 0));
    vec2 point = gl_FragCoord.xy;
    vec2 p0 = clipToScreen(pc.vertices[0], viewportSize);
    vec2 p1 = clipToScreen(pc.vertices[1], viewportSize);
    vec2 p2 = clipToScreen(pc.vertices[2], viewportSize);
    vec2 p3 = clipToScreen(pc.vertices[3], viewportSize);

    int uLines = int(floor(pc.materialOptions.y + 0.5));
    int vLines = int(floor(pc.materialOptions.z + 0.5));
    float halfWidth = pc.shadowOptions.w * 0.5;
    float mask = 0.0;

    for (int i = 0; i <= 256; ++i) {
        if (i > uLines) {
            break;
        }
        float t = float(i) / max(pc.materialOptions.y, 0.0001);
        mask = max(mask, lineMask(point, mix(p0, p1, t), mix(p3, p2, t), halfWidth));
    }

    for (int i = 0; i <= 256; ++i) {
        if (i > vLines) {
            break;
        }
        float t = float(i) / max(pc.materialOptions.z, 0.0001);
        mask = max(mask, lineMask(point, mix(p0, p3, t), mix(p1, p2, t), halfWidth));
    }

    return mask * pc.gridColor.a;
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
