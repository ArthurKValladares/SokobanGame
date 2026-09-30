#version 460

layout(set = 0, binding = 14) uniform sampler2D bloomExtract;

layout(location = 0) out vec4 outColor;

void main()
{
    vec2 extent = vec2(textureSize(bloomExtract, 0));
    vec2 uv = gl_FragCoord.xy / extent;
    vec2 stepUv = vec2(0.0, 1.0 / extent.y);

    vec3 result = texture(bloomExtract, uv).rgb * 0.227027;
    result += texture(bloomExtract, uv + stepUv * 1.384615).rgb * 0.316216;
    result += texture(bloomExtract, uv - stepUv * 1.384615).rgb * 0.316216;
    result += texture(bloomExtract, uv + stepUv * 3.230769).rgb * 0.070270;
    result += texture(bloomExtract, uv - stepUv * 3.230769).rgb * 0.070270;
    outColor = vec4(result, 1.0);
}
