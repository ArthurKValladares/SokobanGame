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
is 3.375 display pixels, independent of model size, camera distance and internal
render scale. Static and animated opaque models use depth-tested expanded
backfaces. Transparent ice uses a contour on the visible surface so its interior
stays transparent. Faceted crystals use a depth mask to keep their contours
around the silhouette. Contours draw before output processing and do not cast
shadows or affect picking.

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

The sky uses three broken cloud banks above a star field. The distant bank is
muted lavender, the middle bank is darker blue-violet, and the near wisps catch
peach light. Gaps between the banks reveal stars throughout the view. Stars are
fixed to world directions and fade behind denser clouds. Three broad haze strata
blend subdued peach light into deeper blue-violet tones as the camera looks
farther down.
Cloud colors are mildly desaturated to stand apart from the islands. Each bank
is a field of separate cumulus puffs: a smooth union of round domes at jittered
lattice points makes sparse clumps, and two finer dome fields scallop their
outlines. The warped simplex billow field is kept as surface texture. It lights
the puffs and slightly frays their rims, but does not decide where clouds are,
because its warp shears an outline into streaks. A fine, shallow warp bends the
dome arcs and two octaves of fine noise erode the rims, so the outlines are
ragged, and a broad thickness field makes parts of each cloud thinner or denser
than the layer's opacity. Analytic gradients keep the lighting smooth during
motion.
Clouds and the atmosphere's broad color variation are anchored at fixed world
heights, preserving depth during camera movement; each cloud bank drifts
independently. Coverage, opacity, haze and both shading colors are set per layer
in the sky shader and can be adjusted with shader hot reload.

## Island mist and light shafts

A thin mist hangs below and around the part of the board the camera frames:
the whole level in a puzzle, the screen in view in the overworld. The
volumetric pass marches it against the sun's shadow map, so the island cuts
a shaft of shadow through it and light pours down past the island's edges and
through its gaps. Sunlit mist takes the sun's warmth and shadowed mist the
cool ambient fill, the same warm/cool split as the surfaces. The mist starts
at the underside of the lowest ground layer and reaches downward only, so it
never covers anything on the board. Its billows drift slowly in world space,
and it looks as thick from a flat camera as from a steep one.

The shafts need **Shadows** on, and they follow the sun: its azimuth turns
them and its tilt sets how far they lean. With the default sun they run down
and to the right of the island on screen, and much of their length falls
below the bottom of a level that fills the view.

Toggle **Island Mist** in the **Lighting** panel. Its values are tunables in
[`IslandMistConfig.hpp`](../src/engine/render/IslandMistConfig.hpp): open
**Tuning > Island mist** in a developer build, and **Save to header** keeps
what you settle on.

| Control | Effect | Default |
| --- | --- | --- |
| Color | Scattering albedo. Warmer values push the lit shafts toward gold. | 1.0, 0.9, 0.82 |
| Density | Thickness at the top of the layer. Raises both the glow and how much it hides the clouds. | 0.06 |
| Scattering Strength | Brightness of the glow without hiding more of the clouds. | 1.8 |
| Ambient Strength | How much ambient fill the mist picks up. Lower values darken the shadowed mist, so shafts stand out. | 0.5 |
| Depth Falloff | How quickly the mist thins downward. Higher values give short, crisp shadows close under the island; lower values give long, soft shafts. | 0.35 |
| Depth | How far down the layer is marched. Keep it near 3 / falloff or deeper. | 10 |
| Reach | Distance outside the level over which the mist fades out. | 10 |
| Sample Count | Steps per ray. Below about 20 the shafts' edges stair-step. | 24 |

The camera looks across the sun's rays rather than along them, so
**Anisotropy** above about 0.3 dims the mist instead of brightening it. The
noise controls match the fog of war's.

## Cloud light

The sun is broken up as if clouds were passing overhead. A slow, drifting
pattern of gaps dims the sunlight wherever a cloud stands in its way, on the
ground and in the air alike, so soft pools of light cross the board and the
mist's even glow separates into shafts. The pattern is laid out along the
sun's rays: a shaft in the air comes down on the pool of light it belongs
to, and both lean with the sun's tilt.

Only the sun is dimmed. Ambient fill, point lights and cast shadows stay as
they are, and nothing on the board is ever brighter than it is under an open
sun. The board gets the broad pattern at a low strength, because a pool of
light strong enough to notice at a glance could be mistaken for something
that matters to the puzzle. The air gets the same pattern at a much higher
strength, with finer breaks inside the gaps that split one broad beam into
several narrow rays. The fog of war is lit like the ground under it.

The shafts show wherever there is air thick enough to show them, which is
the island mist, beside and below the level. With **Island Mist** off only
the pools on the board remain. The atmosphere above the board is a thin
layer seen from overhead and draws no beam over the puzzle: raising
**Atmosphere Density** hazes the board long before one shows. These clouds
are not the ones drawn in the sky below the islands; the two patterns drift
independently.

Toggle **Cloud Light** in the **Lighting** panel. Its values are tunables in
[`CloudLightConfig.hpp`](../src/engine/render/CloudLightConfig.hpp), under
**Tuning > Cloud light**.

| Control | Effect | Default |
| --- | --- | --- |
| Surface Strength | How much of the sun a cloud takes from the board. Keep it low. | 0.3 |
| Medium Strength | The same for the mist and the atmosphere. This is what separates the shafts. | 0.85 |
| Medium Gain | How much brighter the air glows inside a gap, which gives back part of what the clouds take. | 1.35 |
| Scale | Clouds per world unit. 0.09 puts a gap every eleven tiles or so. | 0.09 |
| Coverage | Share of the sky under cloud. | 0.46 |
| Softness | How far a cloud's edge spreads, as a share of the sky. Small values give hard-edged pools. | 0.2 |
| Detail Scale, Detail Strength | Frequency and depth of the finer breaks the air sees. | 0.6, 0.9 |
| Detail Stretch | How round those breaks are. Lower values draw them out along the line of sight, into cleaner rays. | 0.2 |
| Drift Speed, Drift Direction Degrees | How fast the clouds travel, in world units per second, and which way. | 0.35, 25 |

An evidence capture freezes the effect clock, so it shows the clouds where
they start unless `--evidence-animate` is given.

## Ambient motes

Dust and pollen hang in the air over the island. Each mote is a small soft
speck that drifts on the same wind as the cloud light, far more slowly,
wanders a little as it goes and twinkles. Motes show where the sun reaches
them: bright in a pool of light or a shaft, nearly gone under a cloud. They
are ordinary particles, so the board hides the ones behind it and the fog of
war covers the ones over an undiscovered screen.

They are there to make the air look inhabited, never to look like something
to act on, so the defaults keep them tiny, slow and faint. Toggle
**Ambient Motes** in the **Lighting** panel. Their values are tunables in
[`AmbientMotesConfig.hpp`](../src/engine/AmbientMotesConfig.hpp), under
**Tuning > Ambient motes**.

| Control | Effect | Default |
| --- | --- | --- |
| Density | Share of the air's unit cells that hold a mote. | 0.1 |
| Size | Width of a mote's sprite in tiles. The bright disc at its middle is about a third as wide. | 0.14 |
| Color, Opacity, Glow | Colour, opacity and brightness in full sun. A glow above one reaches the bloom. | warm white, 0.7, 1.5 |
| Shade Visibility | How much of a mote is left under a cloud. | 0.2 |
| Drift Speed, Rise Speed | How fast the air carries the motes sideways and upward, in tiles per second. | 0.07, 0.02 |
| Wander Radius, Wander Speed | How far a mote strays from its path, and how quickly. | 0.18, 0.5 |
| Reach, Height, Depth | How far the motes extend beyond the framed board, above the level's tallest layer and below its floor. | 2.5, 1.5, 2 |

## Wind

Grass and foliage sway. Gusts cross the board the way the cloud light drifts,
one after another, and a blade leans downwind as each passes, a little more
at its tip than along its length and not at all at its root. A quick flutter
across the wind keeps neighbouring blades from moving as one. Today the grass
of the tile decorations sways and the crowns of their moss give a little;
stones and everything else stand still.

A model takes part by carrying a `_WIND` weight on its vertices, zero where it
is planted and up to one where it moves the most, which the
[tile decoration kit](../assets/custom/source/tile_decorations/README.md)
writes for its foliage. The sway is in the main view's vertex shader only: a
blade's shadow stays where the blade is planted.

The sway is small and slow on purpose, because a board that moves too much is
hard to read. Toggle **Wind** in the **Lighting** panel. Its values are
tunables in [`WindConfig.hpp`](../src/engine/render/WindConfig.hpp), under
**Tuning > Wind**. An evidence capture freezes the effect clock, so it shows
one moment of a gust unless `--evidence-animate` is given.

| Control | Effect | Default |
| --- | --- | --- |
| Sway Amplitude | How far the tip of a tall blade moves in a full gust, in tiles. Zero stills the grass. | 0.06 |
| Gust Length | Distance from one gust to the next, in tiles. Short gusts ripple; long ones move a whole border at once. | 6 |
| Gust Speed | How fast a gust crosses the board, in tiles per second. | 1.8 |
| Flutter | Share of the sway that is a quick sideways tremble. | 0.25 |

## Bloom

Bloom has two scales. A tight core, a few pixels wide, keeps a highlight
crisp. A wide skirt around it, some twenty pixels wide, is what reads as
glow. Only light brighter than ordinary white blooms, so the board itself
stays as it is and the glow gathers on goal plates, energy, flames and
sunlit cloud.

Bloom has no switch of its own; the game always draws it and thumbnails
never do. Its values are tunables in
[`BloomConfig.hpp`](../src/engine/render/BloomConfig.hpp), under
**Tuning > Bloom**.

| Control | Effect | Default |
| --- | --- | --- |
| Threshold, Soft Knee | How bright a pixel must be to bloom, and how gradually it starts. | 1.0, 0.5 |
| Intensity | Strength of the core. | 0.18 |
| Wide Intensity | Strength of the skirt. Zero turns it off and saves its two passes. | 0.45 |
| Wide Spread | How far the skirt reaches, about seven pixels of width for every unit. Much above three, a bright point starts to break into dots. | 3.0 |

The skirt's width is in pixels, so it looks narrower on a larger display.

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

The surface changes add no render passes and preserve the source assets,
linear HDR lighting, sRGB texture/output handling, tonemapping, and UI
rendering. The mist adds one reduced-resolution pass and one composite. Mesh
silhouettes, proportions, bevels, texture painting, and scene composition
still determine much of the style. Shader tuning can unify color and soften
surface response, while more expressive shapes and authored texture detail
require asset work.
