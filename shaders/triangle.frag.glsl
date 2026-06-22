#version 460

layout(set = 0, binding = 0) uniform sampler2D shadowMap;

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
} pc;

void main()
{
    vec3 color = pc.color.rgb;
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

    outColor = vec4(color, pc.color.a);
}
