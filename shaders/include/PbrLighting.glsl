#ifndef SOKOBAN_PBR_LIGHTING_GLSL
#define SOKOBAN_PBR_LIGHTING_GLSL

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

#endif
