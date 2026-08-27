#version 460

layout(set = 0, binding = 0) uniform sampler2D shadowMap;
layout(set = 0, binding = 1) uniform sampler2D sceneColor;
layout(set = 0, binding = 2) uniform sampler2D modelTextures[MODEL_TEXTURE_COUNT];
layout(set = 0, binding = 3) uniform sampler2D uiFont;
layout(set = 0, binding = 4) uniform sampler2D titleBackground;
layout(set = 0, binding = 8) uniform samplerCubeArray pointShadowMaps;

layout(location = 0) in vec4 inShadowPosition;
layout(location = 1) in float inFaceCoordU;
layout(location = 2) in float inFaceCoordV;
layout(location = 3) in vec3 inNormal;
layout(location = 6) in vec3 inWorldPosition;
layout(location = 7) flat in uint inDrawInstance;
// Not read yet. It arrives here so that the vertex format, the pipelines and
// the interface between the stages are all in place and provably unchanged
// before the lighting that uses it changes everything at once.
layout(location = 8) in vec4 inTangent;
layout(location = 9) in vec2 inUv1;
// Which of the owning model's materials this fragment belongs to, relative to
// the model. draw.passData[0].x makes it absolute.
layout(location = 10) flat in uint inMaterialIndex;
layout(location = 0) out vec4 outColor;

// One draw's parameters, read back by instance index. T1 moved these out of
// push constants so that consecutive draws sharing a pipeline can collapse
// into a single instanced draw.
struct DrawInstance
{
    vec4 vertices[4];
    vec4 passData[4];
    vec4 color;
    vec4 normalAndAmbientRed;
    vec4 sunDirectionAndAmbientGreen;
    vec4 sunRadianceAndAmbientBlue;
    vec4 shadowOptions;
    vec4 materialOptions;
    vec4 gridColor;
    vec4 textureOptions;
};
layout(std430, set = 0, binding = 10) readonly buffer DrawInstances
{
    DrawInstance instances[];
} drawInstances;

#define draw drawInstances.instances[inDrawInstance]

// One glTF material. Mirrors GpuMaterial in VulkanRenderConstants.hpp; the
// static_assert there is what keeps the two from drifting in size, and the
// field comments there are the authority on what each lane means.
struct Material
{
    vec4 baseColorFactor;
    vec4 emissiveAndMetallic;
    vec4 roughnessAlphaFlags;
    vec4 textureAndUvSet;
};
layout(std430, set = 0, binding = 12) readonly buffer Materials
{
    Material entries[];
} materials;

// Entry zero is reserved as the published-nothing fallback: an untextured
// white surface. A base of zero means the model has not published, and its
// materialIndex means nothing yet, so it is ignored rather than added - which
// is what makes the fallback read white instead of some other model's range.
Material modelMaterial()
{
    int base = int(draw.passData[0].x + 0.5);
    return materials.entries[base == 0 ? 0 : base + int(inMaterialIndex)];
}


struct PointLightData
{
    vec4 positionAndRange;
    vec4 colorAndIntensity;
    vec4 shadowOptions;
};
layout(std140, set = 0, binding = 7) uniform SceneFrame
{
    mat4 clipFromWorld;
    mat4 shadowFromWorld;
    vec4 cameraPositionAndNearPlane;
    PointLightData pointLights[8];
    vec4 pointLightMeta;
} frame;

const int MATERIAL_MODE_PROCEDURAL_TEXTURE = 5;

// Set on the opaque pipelines only. Screen-space occlusion estimates *ambient*
// visibility, so multiplying the finished pixel by it - which is what the AO
// composite used to do - darkens direct sunlight as well, which no amount of
// occlusion should. When this is set the alpha channel stops carrying the
// material's alpha and carries the share of this pixel's light that came from
// the ambient term instead, and the composite scales its effect by it.
//
// Only the opaque pipelines can do this: a blended draw needs alpha to mean
// alpha. Those pipelines are created with the alpha channel masked out of
// their colour writes, so a translucent surface inherits the mask of whatever
// opaque geometry it sits in front of, which is the right answer anyway.
layout(constant_id = 0) const bool writeAmbientMask = false;

const vec3 luminanceWeights = vec3(0.2126, 0.7152, 0.0722);


vec3 gaussianBlurredScene(vec2 uv)
{
    const float weights[5] = float[5](1.0, 4.0, 6.0, 4.0, 1.0);
    vec2 viewportSize = vec2(textureSize(sceneColor, 0));
    vec2 texel = abs(draw.materialOptions.w) / viewportSize;
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

void applyEditorPreviewDither()
{
    if (draw.materialOptions.w >= 0.0) {
        return;
    }

    const float pixelScale = 2.0;
    const float coverage = 0.56;
    ivec2 ditherPixel = ivec2(floor(gl_FragCoord.xy / pixelScale));
    if (bayer8x8(ditherPixel) >= coverage) {
        discard;
    }
}

float gridMask()
{
    if (draw.gridColor.a <= 0.0 || draw.shadowOptions.w <= 0.0 || draw.materialOptions.y <= 0.0 || draw.materialOptions.z <= 0.0) {
        return 0.0;
    }

    vec2 faceCoord = vec2(inFaceCoordU, inFaceCoordV);
    vec2 wrapped = fract(faceCoord);
    vec2 distanceToLine = min(wrapped, 1.0 - wrapped);
    vec2 coordPerPixel = max(fwidth(faceCoord), vec2(0.00001));
    vec2 halfWidth = coordPerPixel * draw.shadowOptions.w * 0.5;
    vec2 feather = coordPerPixel;
    vec2 line = 1.0 - smoothstep(halfWidth, halfWidth + feather, distanceToLine);
    return max(line.x, line.y) * draw.gridColor.a;
}

float shadowFactor(vec4 shadowPosition, float diffuse)
{
    if (draw.shadowOptions.x <= 0.5 || diffuse <= 0.0 || abs(shadowPosition.w) <= 0.0001) {
        return 1.0;
    }

    vec3 shadowCoord = shadowPosition.xyz / shadowPosition.w;
    vec2 shadowUv = shadowCoord.xy * 0.5 + 0.5;
    if (any(lessThan(shadowUv, vec2(0.0))) ||
        any(greaterThan(shadowUv, vec2(1.0))) ||
        shadowCoord.z < 0.0 ||
        shadowCoord.z > 1.0) {
        return 1.0;
    }

    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float shadowedSamples = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float closestDepth = texture(shadowMap, shadowUv + vec2(float(x), float(y)) * texel).r;
            shadowedSamples += shadowCoord.z - draw.shadowOptions.z > closestDepth ? 1.0 : 0.0;
        }
    }

    float shadowAmount = shadowedSamples / 9.0;
    return 1.0 - shadowAmount * draw.shadowOptions.y;
}

float pointShadowWorldDistance(
    float depth, float nearPlane, float farPlane)
{
    float denominator = farPlane - depth * (farPlane - nearPlane);
    return denominator > 0.000001
        ? farPlane * nearPlane / denominator
        : farPlane;
}

float pointShadowFactor(
    int lightIndex, vec3 fromLight, vec3 surfaceNormal)
{
    PointLightData light = frame.pointLights[lightIndex];
    if (light.shadowOptions.x <= 0.5) {
        return 1.0;
    }
    const float nearPlane = 0.05;
    float farPlane = max(light.positionAndRange.w, nearPlane + 0.001);
    float majorDistance = max(
        abs(fromLight.x), max(abs(fromLight.y), abs(fromLight.z)));
    if (majorDistance <= nearPlane || majorDistance >= farPlane) {
        return 1.0;
    }
    float texelAngle = 2.0 / float(textureSize(pointShadowMaps, 0).x);
    vec3 direction = normalize(fromLight);
    // The authored bias is a world-space minimum. Increase it at grazing
    // angles, where rasterized depth changes fastest across the surface.
    float facing = clamp(dot(normalize(surfaceNormal), -direction), 0.0, 1.0);
    float worldBias = max(light.shadowOptions.z, 0.0) *
        (1.0 + 2.0 * (1.0 - facing));
    vec3 tangent = normalize(cross(
        abs(direction.z) < 0.9 ? vec3(0.0, 0.0, 1.0)
                               : vec3(0.0, 1.0, 0.0),
        direction));
    vec3 bitangent = cross(direction, tangent);
    vec3 offsets[5] = vec3[5](
        vec3(0.0), tangent, -tangent, bitangent, -bitangent);
    float shadowed = 0.0;
    for (int sampleIndex = 0; sampleIndex < 5; ++sampleIndex) {
        vec3 sampleDirection = direction +
            offsets[sampleIndex] * texelAngle;
        float closestDepth = texture(
            pointShadowMaps,
            vec4(sampleDirection, light.shadowOptions.y)).r;
        float closestDistance = pointShadowWorldDistance(
            closestDepth, nearPlane, farPlane);
        shadowed += majorDistance - worldBias > closestDistance
            ? 1.0
            : 0.0;
    }
    return 1.0 - shadowed / 5.0 *
        clamp(light.shadowOptions.w, 0.0, 1.0);
}

// Cook-Torrance GGX. F3c replaced a wrapped-diffuse Blinn-Phong whose gloss
// came from two scene-wide knobs, so every surface in a level was equally
// shiny no matter what its glTF said.
//
// The pi that belongs under the Lambert term is folded into the light instead
// of divided out of the surface, which is the same thing as scaling every
// authored light intensity by pi. It keeps the swap from darkening every
// existing level threefold, and the specular below carries the matching pi so
// the two stay in the proportion the physics puts them in.
const float pi = 3.14159265359;

float distributionGgx(float normalDotHalf, float roughness)
{
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float denominator =
        normalDotHalf * normalDotHalf * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(pi * denominator * denominator, 0.0001);
}

// Smith with the direct-lighting remap of k. Height-correlated would be a
// little more accurate and costs a divide this does not need to spend.
float geometrySmith(float normalDotView, float normalDotLight, float roughness)
{
    float k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
    float view = normalDotView / (normalDotView * (1.0 - k) + k);
    float light = normalDotLight / (normalDotLight * (1.0 - k) + k);
    return view * light;
}

vec3 fresnelSchlick(float cosine, vec3 f0)
{
    float f = clamp(1.0 - cosine, 0.0, 1.0);
    float fSquared = f * f;
    return f0 + (1.0 - f0) * (fSquared * fSquared * f);
}

// Kept apart so the ambient mask can weigh the diffuse half on its own:
// occlusion estimates ambient visibility and has no business scaling a direct
// reflection, which is the same contract F2b established.
struct Shaded
{
    vec3 diffuse;
    vec3 specular;
};

Shaded shadeLight(
    vec3 normal,
    vec3 viewDirection,
    vec3 lightDirection,
    vec3 radiance,
    vec3 diffuseAlbedo,
    vec3 f0,
    float roughness)
{
    Shaded result = Shaded(vec3(0.0), vec3(0.0));
    float normalDotLight = dot(normal, lightDirection);
    if (normalDotLight <= 0.0) {
        return result;
    }
    float normalDotView = max(dot(normal, viewDirection), 0.0001);
    vec3 halfVector = normalize(lightDirection + viewDirection);
    vec3 fresnel = fresnelSchlick(
        max(dot(halfVector, viewDirection), 0.0), f0);
    float distribution = distributionGgx(
        max(dot(normal, halfVector), 0.0), roughness);
    float geometry = geometrySmith(normalDotView, normalDotLight, roughness);

    vec3 incoming = radiance * normalDotLight;
    // The pi here is the one folded into the light above.
    result.specular = incoming * fresnel * distribution * geometry * pi /
        max(4.0 * normalDotView * normalDotLight, 0.0001);
    result.diffuse = incoming * (vec3(1.0) - fresnel) * diffuseAlbedo;
    return result;
}

void main()
{
    applyEditorPreviewDither();

    // Read for every draw, not just the primitive-materials one. A draw with
    // no model behind it has a zero base and lands on the reserved fallback:
    // white factor, no texture, metallic 0, roughness 1. So this is a no-op
    // for tiles, UI and particles, and the lighting below needs no branch to
    // find a metallic and a roughness for them.
    Material material = modelMaterial();
    // glTF says the base colour factor multiplies the base colour texture.
    // Alpha only counts on a BLEND material: an OPAQUE one ignores it, and
    // this engine picks a model's pipeline from the tile rather than from the
    // material, so an authored alpha on an opaque material would otherwise
    // punch a hole through a surface the file says is solid.
    vec4 materialColor = draw.color;
    materialColor.rgb *= material.baseColorFactor.rgb;
    if (int(material.roughnessAlphaFlags.z + 0.5) == 2) {
        materialColor.a *= material.baseColorFactor.a;
    }
    int materialMode = int(draw.textureOptions.x + 0.5);
    if (materialMode == 3) {
        vec2 uv = draw.gridColor.xy + vec2(inFaceCoordU, inFaceCoordV);
        materialColor.a *= texture(uiFont, uv).r;
    } else if (materialMode == 6) {
        vec2 uv = draw.gridColor.xy + vec2(inFaceCoordU, inFaceCoordV);
        // Colour only. The scene target's alpha is the ambient mask now, not
        // an opacity, and folding it into this composite would feather the
        // preview by how much ambient light happened to reach it.
        materialColor.rgb *= texture(sceneColor, uv).rgb;

        // Scene-image UI is the preserved main view composited over the
        // preview. A signed-distance rounded rectangle makes the preview's
        // outside edge genuinely transparent and gives corners one smooth,
        // continuous curve instead of a stack of rectangular bands.
        vec2 uvSpan = max(draw.materialOptions.yz, vec2(0.00001));
        vec2 localPosition =
            vec2(inFaceCoordU, inFaceCoordV) / uvSpan;
        vec2 rectSize = max(draw.shadowOptions.xy, vec2(1.0));
        float featherWidth = max(draw.shadowOptions.z, 0.001);
        float cornerRadius = clamp(
            draw.shadowOptions.w,
            0.0,
            min(rectSize.x, rectSize.y) * 0.5);
        vec2 centeredPosition =
            localPosition * rectSize - rectSize * 0.5;
        vec2 distanceToCorner = abs(centeredPosition) -
            (rectSize * 0.5 - vec2(cornerRadius));
        float signedDistance =
            length(max(distanceToCorner, vec2(0.0))) +
            min(max(distanceToCorner.x, distanceToCorner.y), 0.0) -
            cornerRadius;
        float normalizedDistance = clamp(
            -signedDistance / featherWidth,
            0.0,
            1.0);
        float endpointSoftened = smoothstep(
            0.0,
            1.0,
            normalizedDistance);
        // A normalized logarithmic remap lifts the quiet first part of the
        // Hermite curve and softens its abrupt-looking middle without losing
        // the zero-slope endpoints that prevent visible seams.
        const float logarithmicStrength = 2.0;
        float previewOpacity = log(
            1.0 + logarithmicStrength * endpointSoftened) /
            log(1.0 + logarithmicStrength);
        materialColor.a *= 1.0 - previewOpacity;
    } else if (materialMode == 4) {
        vec2 uv = draw.gridColor.xy + vec2(inFaceCoordU, inFaceCoordV);
        materialColor *= texture(titleBackground, uv);
    } else if (materialMode == 7) {
        vec2 uv = draw.gridColor.xy + vec2(inFaceCoordU, inFaceCoordV);
        int textureIndex = clamp(
            int(draw.textureOptions.y + 0.5) - 1,
            0,
            MODEL_TEXTURE_COUNT - 1);
        materialColor *= texture(modelTextures[textureIndex], uv);
    } else if (materialMode == 2) {
        // The factor is applied above for every mode. Only the texture is
        // per-mode: mode 1's comes from the manifest's single-texture
        // override, so the glTF's own base colour texture is deliberately
        // not consulted there.
        int materialTexture = int(material.textureAndUvSet.x + 0.5);
        if (materialTexture != 0) {
            int textureIndex = clamp(
                materialTexture - 1, 0, MODEL_TEXTURE_COUNT - 1);
            // Set 1 is the second unwrap; the loader falls it back to set 0
            // on a mesh that has only one, so this never reads nothing.
            vec2 uv = int(material.textureAndUvSet.y + 0.5) == 1
                ? inUv1
                : vec2(inFaceCoordU, inFaceCoordV);
            if ((int(material.roughnessAlphaFlags.w + 0.5) & 1) != 0) {
                uv.y = fract(uv.y + draw.materialOptions.y);
            }
            materialColor *= texture(modelTextures[textureIndex], uv);
        }
    } else if (materialMode == MATERIAL_MODE_PROCEDURAL_TEXTURE) {
        // Procedural quads use a one-based texture handle because zero means
        // that no runtime texture was resolved.
        float selectedTexture = draw.textureOptions.y - 1.0;
        int textureIndex = clamp(
            int(selectedTexture + 0.5),
            0,
            MODEL_TEXTURE_COUNT - 1);
        materialColor *= texture(modelTextures[textureIndex], vec2(inFaceCoordU, inFaceCoordV));
    } else if (materialMode == 1) {
        int textureIndex = clamp(
            int(draw.materialOptions.z + 0.5),
            0,
            MODEL_TEXTURE_COUNT - 1);
        materialColor *= texture(modelTextures[textureIndex], vec2(inFaceCoordU, inFaceCoordV));
    }
    vec3 color = mix(materialColor.rgb, draw.gridColor.rgb, gridMask());
    // Stays zero for anything unlit - the grid overlay, the 2D board, UI -
    // so occlusion cannot touch surfaces that have no ambient term to occlude.
    float ambientMask = 0.0;
    if (length(inNormal) > 0.0001) {
        vec3 normal = normalize(inNormal);
        vec3 lightDirection = length(draw.sunDirectionAndAmbientGreen.xyz) > 0.0001
            ? normalize(draw.sunDirectionAndAmbientGreen.xyz)
            : vec3(0.0, 0.0, 1.0);
        float lambertDiffuse = max(dot(normal, lightDirection), 0.0);
        vec3 ambient = vec3(
            draw.normalAndAmbientRed.w,
            draw.sunDirectionAndAmbientGreen.w,
            draw.sunRadianceAndAmbientBlue.w);
        float shadow = shadowFactor(inShadowPosition, lambertDiffuse);
        float skyFill = smoothstep(-0.35, 1.0, normal.z);

        // The material's own gloss, where the whole level used to share one
        // exponent. Roughness has a floor because a perfect mirror is a
        // delta function no point light can hit.
        float metallic = clamp(material.emissiveAndMetallic.w, 0.0, 1.0);
        float roughness = clamp(material.roughnessAlphaFlags.x, 0.045, 1.0);
        // Dielectrics reflect about 4% head-on; a metal reflects its own
        // colour and has no diffuse lobe at all.
        vec3 f0 = mix(vec3(0.04), color, metallic);
        vec3 diffuseAlbedo = color * (1.0 - metallic);
        // A scene-wide dial on the specular half, kept from the old model so
        // a level that wanted a flatter look still has the knob. Its
        // companion, specularPower, is gone: roughness is the exponent now.
        float specularStrength = max(draw.textureOptions.z, 0.0);

        vec3 diffuseLight = vec3(0.0);
        vec3 specularLight = vec3(0.0);
        // The real direction from the surface to the camera. This used
        // to be a compiled-in constant matching one fixed isometric camera,
        // so every specular highlight was correct only from that angle and
        // wrong everywhere else the camera could go.
        vec3 viewDirection = normalize(
            frame.cameraPositionAndNearPlane.xyz - inWorldPosition);
        int pointLightCount = clamp(int(frame.pointLightMeta.x + 0.5), 0, 8);
        for (int lightIndex = 0; lightIndex < pointLightCount; ++lightIndex) {
            PointLightData pointLight = frame.pointLights[lightIndex];
            vec3 toLight = pointLight.positionAndRange.xyz - inWorldPosition;
            float distanceToLight = length(toLight);
            float range = max(pointLight.positionAndRange.w, 0.001);
            if (distanceToLight <= 0.0001 || distanceToLight >= range) {
                continue;
            }
            vec3 pointDirection = toLight / distanceToLight;
            float pointLambert = max(dot(normal, pointDirection), 0.0);
            if (pointLambert <= 0.0) {
                continue;
            }
            float normalizedDistance = distanceToLight / range;
            float rangeWindow = max(
                1.0 - normalizedDistance * normalizedDistance *
                    normalizedDistance * normalizedDistance,
                0.0);
            float attenuation = rangeWindow * rangeWindow /
                max(distanceToLight * distanceToLight, 0.04);
            float pointShadow = pointShadowFactor(
                lightIndex, -toLight, normal);
            vec3 radiance = pointLight.colorAndIntensity.rgb *
                pointLight.colorAndIntensity.w * attenuation * pointShadow;
            Shaded point = shadeLight(
                normal, viewDirection, pointDirection, radiance,
                diffuseAlbedo, f0, roughness);
            diffuseLight += point.diffuse;
            specularLight += point.specular;
        }
        Shaded sun = shadeLight(
            normal,
            viewDirection,
            lightDirection,
            draw.sunRadianceAndAmbientBlue.rgb * shadow,
            diffuseAlbedo,
            f0,
            roughness);
        diffuseLight += sun.diffuse;
        specularLight += sun.specular;

        // A hemispheric approximation, kept from the old model: an up-facing
        // surface sees more of the sky. Both halves are here - the f0 half is
        // what stops a metal from going black wherever no light reaches it
        // directly, standing in for the environment probe this engine does
        // not have yet.
        //
        // specularStrength deliberately does not reach it. That knob dials
        // the gloss a level wants; this is not gloss, it is the only ambient
        // a metal gets, and letting the slider reach zero would render a
        // metallic surface black.
        vec3 ambientTerm = ambient * (1.0 + skyFill * 0.35);
        vec3 ambientContribution = ambientTerm * (diffuseAlbedo + f0);

        color = diffuseLight + ambientContribution +
            specularLight * specularStrength;

        // The share of this pixel's light that came from ambient, which is
        // what the SSAO composite scales by.
        //
        // Both halves of the ambient fill count: the f0 half is ambient by
        // construction, and leaving it out - which is what "specular is
        // excluded from both sides" would have meant here - made a metallic
        // surface report a smaller ambient share than it had, and a fully
        // metallic one report zero over zero and take no occlusion at all.
        // Direct light, specular included, stays in the denominator only, so
        // a pixel dominated by a highlight is occluded less. That is the same
        // intent F2b had; only the arithmetic that serves it has changed.
        ambientMask = clamp(
            dot(ambientContribution, luminanceWeights) /
                max(dot(color, luminanceWeights), 0.0001),
            0.0,
            1.0);
    }

    int editorHighlight = int(draw.textureOptions.y + 0.5);
    if (draw.gridColor.w < 0.0 && editorHighlight > 0) {
        vec3 highlightColor = editorHighlight == 2
            ? vec3(1.0, 0.48, 0.08)
            : vec3(0.08, 0.88, 1.0);
        float tintStrength = editorHighlight == 2 ? 0.48 : 0.38;
        float pulse = 0.5 + 0.5 * sin(draw.materialOptions.w * 3.5);

        // A narrow screen-space sweep gives the opaque tint some life without
        // changing the model's geometry, texture, depth, or silhouette.
        float sweepPhase = fract(
            (gl_FragCoord.x + gl_FragCoord.y) * 0.0025 -
            draw.materialOptions.w * 0.55);
        float glimmer = 1.0 - smoothstep(0.0, 0.075, abs(sweepPhase - 0.5));

        color = mix(color, highlightColor, tintStrength + pulse * 0.04);
        color += mix(vec3(1.0), highlightColor, 0.35) * glimmer * 0.32;
        outColor = vec4(color, writeAmbientMask ? ambientMask : 1.0);
        return;
    }

    if (draw.materialOptions.x > 0.5) {
        vec2 uv = gl_FragCoord.xy / vec2(textureSize(sceneColor, 0));
        vec3 blurred = gaussianBlurredScene(uv);
        outColor = vec4(
            mix(blurred, color, materialColor.a),
            writeAmbientMask ? ambientMask : 1.0);
        return;
    }

    outColor = vec4(
        color, writeAmbientMask ? ambientMask : materialColor.a);
}
