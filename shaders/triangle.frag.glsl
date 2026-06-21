#version 460

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants
{
    vec4 vertices[4];
    vec4 color;
    vec4 normalAndAmbientRed;
    vec4 sunDirectionAndAmbientGreen;
    vec4 sunRadianceAndAmbientBlue;
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
        color *= ambient + pc.sunRadianceAndAmbientBlue.rgb * diffuse;
    }

    outColor = vec4(color, pc.color.a);
}
