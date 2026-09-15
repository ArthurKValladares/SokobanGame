# Packet 5 water-derivative evidence

Recorded September 14, 2026 on an NVIDIA GeForce RTX 4060 Laptop GPU at 2880x1800. The evidence-only water strip crosses the board edge, placing neighboring fragments over opaque scene depth and clear-background depth. This directly exercises the `geometryPresent` branch boundary.

The committed shader evaluates `dFdx` and `dFdy` for the projected pattern before that branch. It carries those derivatives through each sine/cosine warp by the chain rule, derives the cellular boundary's gradient from the two selected weighted distances, and uses the projected gradient magnitude as the anti-aliasing footprint. The two nine-cell searches remain inside the branch and are skipped over clear background.

## Image comparison

The old shader and revised shader were compiled separately while using the same revised boundary-crossing fixture. Each run froze simulation before capture.

| Render target | AA | Changed pixels | Maximum channel difference |
| --- | ---: | ---: | ---: |
| 2880x1800 | 4x MSAA | 64 and 67 of 5,184,000 in two matched comparisons | 2 |
| 1440x900 | 1x MSAA | 1 of 1,296,000 | 1 |

No seam or other visible edge artifact appeared in the inspected images. The tiny numerical differences are comparable to the two-pixel variation observed between repeated baseline captures.

## GPU timing

Three matched 600-frame Release runs at 100% scale and 4x MSAA produced these medians:

| Metric | Old shader | Revised shader | Difference |
| --- | ---: | ---: | ---: |
| GPU scene translucency average | 1.738 ms | 1.677 ms | -3.5% |
| GPU scene translucency p95 | 3.728 ms | 3.682 ms | -1.2% |
| GPU frame average | 6.791 ms | 6.930 ms | +2.0% |

The individual timing ranges overlap and the run-to-run results were bimodal, consistent with mobile-GPU clock and power variation. This evidence supports no measurable regression; it does not claim a speedup.

## Validation

- `glslc` compiled the optimized Vulkan 1.3 fragment shader and `spirv-val` accepted the generated module.
- A 240-frame Debug run with `--require-validation --evidence-msaa 4 --evidence-water` completed without Vulkan usage errors. It retained the project's known unused-shader-output warnings.
- The complete Debug and Release warning-as-error builds and their 80 registered tests passed.

The image and timing runs cover two render scales and two MSAA modes at the deterministic evidence camera. This machine provides one GPU vendor. Additional camera angles, preview/dither paths, and vendors remain human release-matrix coverage rather than claims made by this packet.
