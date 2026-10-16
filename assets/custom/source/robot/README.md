# Robot platform

The editable source is [`tools/make_robot_model.py`](../../../../tools/make_robot_model.py).
Run `python tools/make_robot_model.py` at the repository root to reproduce
`assets/custom/models/robot.glb`; `--preview` also refreshes `preview.png` here.
Add `--preview-phase 0.137` to inspect a particular drive pose.
The model uses the shared flat-shaded mesh primitive helper from
`tools/make_rotator_models.py`. Generation only requires Python's standard
library. The optional preview requires Pillow and NumPy and uses depth-tested
triangle rasterization so overlapping armor and lamp sockets display correctly.

The silhouette follows the squat mechanical unit at the right of the user's
supplied image: a broad chamfered crate with a flat octagonal top. The current
art uses a rustic, steam-powered construction: aged bronze boiler armor,
tarnished brass shoulders, riveted wrought-iron straps, exposed copper pipes,
side transmission cogs, a pressure gauge and a valve handwheel. Open linked
iron tracks reveal oxidized rollers and brass axle caps. The rounded lanterns
replace modern rectangular lights; warm honey light marks the leading end,
and dim ember glass marks the trailing end.

Six inset oak boards form the deck, with narrow seams, grain cuts and nail
heads. Both opposing axis arrows are weathered cream paint. The deck is at
logical height 1 so a character standing in the cell above meets the top.
Paint, nails and roof fasteners remain below height 1.005, and the mechanical
details stay below the deck and inside the unit footprint. Nothing blocks a
rider. Its source heading is south (+Y), matching the engine's
`facingQuarterTurns` convention. Runtime direction changes rotate the model
to exchange its leading and trailing lanterns.

Coordinates span one cell and the manifest preserves source scale. All twelve
PBR materials are embedded colors without textures. Native metallic/roughness
and emissive lamp factors survive both gameplay and editor rendering. Palette
thumbnails are baked by the game's normal `--bake-tile-thumbnails` workflow.

The stable material slots are 0 bronze armor, 1 brass rim, 2 iron track links,
3 dark iron straps/recesses, 4 cream paint/gauge face, 5 leading honey lantern
glass, 6 trailing ember lantern glass, 7 brass fittings/rivets, 8 weathered oak,
9 darker oak/grain, 10 copper pipes, and 11 rust patina. Both deck arrows use
slot 4. The generator validates the footprint and standing-height bounds
before writing the GLB, and its output is deterministic.

## Drive rig and distance contract

The Robot model uses the existing GPU skinning path. One mesh contains twelve
material primitives, with one rigid bone influence of weight 1 per vertex.
There are 57 joints and 58 skeleton nodes: the fixed `RobotBody`, 24 circulating
shoes on each track, three rollers on each side, and two side transmission
cogs. The body includes the deck, arrows, lanterns, straps, pipes, gauge and
valve. These remain completely still while the running gear moves. Shoes,
rollers and cogs share the model's draw rather than adding separate object
draws for every link.

`RobotDrive` is animation index 0 in the same GLB, named `Drive`. Its period is
exactly **2.0 seconds**. Clip time is signed physical track distance: **one
second advances the belt by one tile**. The closed capsule loop has two
straight runs of length 0.5 and two semicircles of radius `1 / (2 * pi)`, for
an exact loop length of 2. Positive time moves the lower shoes toward source
-Y, matching a chassis driving toward source +Y. Negative time runs the
mechanism backward. Wrap the phase modulo 2, and stop advancing it whenever
the robot stops translating.

Rollers turn two full revolutions over the clip; the side cogs turn two in the
opposite direction. Each channel has 129 keys across the period, preserving
the revolutions and following the curved track ends. Quaternion signs are
kept continuous between neighboring keys. The first and final poses match,
so the belt recirculates without a reset. The generator analytically checks
the rotating parts' full radial bounds as well as the bind pose, covering
every intermediate keyframe interpolation while preserving the unit footprint.
