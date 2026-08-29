#version 460

layout(set = 0, binding = 5) uniform sampler2D depthTexture;
layout(location = 0) out vec4 outAo;

// viewFromClip and clipFromView are exact inverses of the main scene's
// projection. params: x = strength (composite only), y = radius in scene
// units, z = bias in scene units, w = composite debug mode.
layout(push_constant) uniform PushConstants
{
    mat4 viewFromClip;
    mat4 clipFromView;
    layout(offset = 128) vec4 params;
} pc;

// Interleaved gradient noise: cheap per-pixel randomization without a noise
// texture; the composite blur averages it away.
float gradientNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(
        0.06711056 * pixel.x + 0.00583715 * pixel.y));
}

vec3 reconstructViewPosition(vec2 uv, float depth)
{
    // The render pass uses a negative-height viewport: framebuffer UV points
    // down while NDC y points up. Vulkan depth is already NDC z in [0, 1].
    vec4 view = pc.viewFromClip * vec4(
        uv.x * 2.0 - 1.0,
        1.0 - uv.y * 2.0,
        depth,
        1.0);
    return view.xyz / max(abs(view.w), 0.000001) * sign(view.w);
}

vec2 projectViewPosition(vec3 viewPosition)
{
    vec4 clip = pc.clipFromView * vec4(viewPosition, 1.0);
    vec2 ndc = clip.xy / max(abs(clip.w), 0.000001) * sign(clip.w);
    return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

vec3 viewNormal(vec3 centerPosition)
{
    // Position derivatives provide the geometric view-space normal without a
    // G-buffer. Orient it toward the camera at the view-space origin so the
    // sample hemisphere consistently lies outside the visible surface.
    vec3 normal = cross(dFdx(centerPosition), dFdy(centerPosition));
    float magnitudeSquared = dot(normal, normal);
    if (magnitudeSquared <= 0.00000001) {
        return vec3(0.0, 0.0, -1.0);
    }
    normal *= inversesqrt(magnitudeSquared);
    return dot(normal, -centerPosition) < 0.0 ? -normal : normal;
}

void main()
{
    vec2 texel = 1.0 / vec2(textureSize(depthTexture, 0));
    vec2 uv = gl_FragCoord.xy * texel;
    float centerDepth = texture(depthTexture, uv).r;
    vec3 centerPosition = reconstructViewPosition(uv, centerDepth);
    // Derivatives must execute for every invocation in the quad. Returning a
    // background lane first would make neighboring silhouette normals
    // undefined on exactly the pixels where stable reconstruction matters.
    vec3 normal = viewNormal(centerPosition);
    if (centerDepth >= 0.9999) {
        outAo = vec4(1.0);
        return;
    }

    float radius = max(pc.params.y, 0.0001);
    float bias = max(pc.params.z, 0.0);

    const int sampleCount = 12;
    const float goldenAngle = 2.39996323;
    float rotation = gradientNoise(gl_FragCoord.xy) * 6.2831853;
    vec3 randomDirection = vec3(cos(rotation), sin(rotation), 0.0);
    vec3 tangent = randomDirection -
        normal * dot(randomDirection, normal);
    if (dot(tangent, tangent) <= 0.000001) {
        tangent = cross(
            normal,
            abs(normal.z) < 0.9
                ? vec3(0.0, 0.0, 1.0)
                : vec3(0.0, 1.0, 0.0));
    }
    tangent = normalize(tangent);
    vec3 bitangent = cross(normal, tangent);

    float occlusion = 0.0;
    for (int i = 0; i < sampleCount; ++i) {
        float fraction = (float(i) + 0.5) / float(sampleCount);
        float diskRadius = sqrt(fraction);
        float angle = float(i) * goldenAngle;
        vec3 direction =
            tangent * (cos(angle) * diskRadius) +
            bitangent * (sin(angle) * diskRadius) +
            normal * sqrt(max(1.0 - diskRadius * diskRadius, 0.0));
        float sampleDistance = radius * mix(
            0.2, 1.0, fraction * fraction);
        vec3 proposedPosition =
            centerPosition + direction * sampleDistance;
        vec2 sampleUv = projectViewPosition(proposedPosition);
        if (any(lessThan(sampleUv, vec2(0.0))) ||
            any(greaterThan(sampleUv, vec2(1.0)))) {
            continue;
        }

        float sampledDepth = texture(depthTexture, sampleUv).r;
        if (sampledDepth >= 0.9999) {
            continue;
        }
        vec3 actualPosition =
            reconstructViewPosition(sampleUv, sampledDepth);
        float distanceFromCenter =
            length(actualPosition - centerPosition);
        float rangeWeight = 1.0 - smoothstep(
            radius, radius * 2.0, distanceFromCenter);
        // View-space +Z points away from the camera. Geometry in front of the
        // proposed hemisphere sample therefore has a smaller z.
        float isOccluded = step(
            actualPosition.z, proposedPosition.z - bias);
        occlusion += isOccluded * rangeWeight;
    }

    float ao = clamp(
        1.0 - occlusion / float(sampleCount) * 1.6,
        0.0,
        1.0);
    outAo = vec4(ao, ao, ao, 1.0);
}
