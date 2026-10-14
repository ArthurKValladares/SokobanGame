# Stylized rendering

The default look combines a warm peach sun with cool periwinkle ambient fill,
so shadowed surfaces retain color. Softer diffuse shading, reduced normal-map
detail, and broader material highlights make the existing models read more
like painted forms. The same surface treatment applies to model and terrain
materials, including point-light illumination.

Interactive pieces have a thin, cool dark-blue contour: heroes and enemies,
movable rocks/mirrors/ice, controls, platforms and the other functional puzzle
objects. Ground, walls, rails, decorative blocks and freely placed decorations
remain unoutlined, even when a decoration reuses an interactive object's model.
Energy volumes, auras and destination ghosts retain their own effects.

Outline color and width are in
[`OutlineConfig.hpp`](../src/engine/render/OutlineConfig.hpp). The default width
is 1.5 display pixels, independent of model size, camera distance and internal
render scale. Static and animated opaque models use depth-tested expanded
backfaces; transparent ice uses a contour on the visible surface so its interior
stays transparent. Contours draw within the existing scene passes, before
particles and output processing, and do not cast shadows or affect picking.

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

The sky uses three broken cloud banks above a smooth layer of deep air. The
distant bank is muted lavender, the middle bank is darker blue-violet, and the
near wisps catch peach sunlight. Reduced coverage and opacity leave openings
onto a darker lower atmosphere. Three broad haze strata below the clouds blend
subdued peach light near the top into deeper blue-violet tones farther down.
Downward views reveal more of the dark air, with smooth transitions between
depths and independent world-space parallax in the broad color variation.
Cloud colors are mildly desaturated to stand apart from the islands. Rounded
simplex billows, rotated detail octaves and broad domain warping prevent square
formations; analytic gradients keep their lighting smooth during motion.
Clouds and the atmosphere's broad color variation are anchored at fixed world
heights, preserving depth during camera movement; each cloud bank drifts
independently. Coverage, opacity, haze and both shading colors are set per layer
in the sky shader and can be adjusted with shader hot reload.

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
