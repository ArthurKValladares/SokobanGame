# Stylized rendering

The default look combines a warm peach sun with cool periwinkle ambient fill,
so shadowed surfaces retain color. Softer diffuse shading, reduced normal-map
detail, and broader material highlights make the existing models read more
like painted forms. The same surface treatment applies to model and terrain
materials, including point-light illumination.

## Live tuning

Open the **Lighting** panel in a developer build. Start with **Sun Color**,
**Ambient Color**, their intensities, and **Shadow Opacity** to set the color
and contrast of the scene. Sun azimuth and tilt control the direction and
length of cast shadows. Ambient fill remains visible where direct light is
occluded; increasing shadow opacity strengthens the warm/cool separation.

| Control | Effect | Default |
| --- | --- | --- |
| Diffuse Softness | Lifts grazing light on lit surfaces for rounder forms; the terminator remains at zero. | 0.35 |
| Normal Detail | Blends from geometric normals at 0 to full normal-map detail at 1. | 0.65 |
| Roughness Floor | Sets a minimum material roughness, broadening sharp highlights. | 0.38 |

All three sliders range from 0 to 1. Try the lighting colors first, then adjust
surface detail while watching both a character and the ground. Existing
**Specular Strength**, **Model Shadow Receive**, AO, and atmosphere controls
remain available for balancing gloss, self-shadowing, contact depth, and haze.

Lighting defaults are in
[`LightingConfig.hpp`](../src/engine/render/LightingConfig.hpp). The panel
edits the current session; its values are separate from the Tuning tab's
registered **Save to header** controls. Change the constants and rebuild to
retain a new default. Water is tuned independently in
[`WaterConfig.hpp`](../src/engine/render/WaterConfig.hpp); the procedural sky
palette lives in [`sky_clouds.frag.glsl`](../shaders/sky_clouds.frag.glsl).

## Shader behavior and neutral settings

Set **Diffuse Softness = 0**, **Normal Detail = 1**, and **Roughness Floor = 0**
to recover neutral surface shading. Lighting colors and intensities remain
separate choices. Standalone frames default to these neutral surface values.

The diffuse response is continuous, without cel bands. For a positive
normal/light dot product `ndl`, the shader evaluates:

```glsl
float response = 1.0 - 0.5 * softness;
float diffuse = ndl / (ndl * (1.0 - response) + response);
```

Zero softness returns `ndl`. Back-facing surfaces receive no direct light.
Material metallic values and GGX specular shading remain in use; the
roughness control only raises the minimum roughness.

These changes add no render passes and preserve the source assets, linear HDR
lighting, sRGB texture/output handling, tonemapping, and UI rendering. Mesh
silhouettes, proportions, bevels, texture painting, and scene composition
still determine much of the style. Shader tuning can unify color and soften
surface response, while more expressive shapes and authored texture detail
require asset work.
