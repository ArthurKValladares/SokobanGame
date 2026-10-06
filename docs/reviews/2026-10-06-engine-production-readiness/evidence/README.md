# Verification evidence

Captured October 6, 2026, while assessing commit `2aa898266a50a9e66593b375a001b225e467e208`.

These runs use existing configured binaries. No fresh build or hardware rendering run was performed for this assessment. They corroborate the source assessment but do not certify that every executable matches the assessed commit.

## Selected optimized-editor suite

```powershell
ctest --test-dir out/dev-fast --output-on-failure --no-tests=error --timeout 60 -j4 -E 'vulkan_smoke|application_validation_teardown|shipping_package_validation'
```

Result: 90 of 92 selected suites passed. See [full output](engine-readiness-dev-fast-tests.txt).

The configured registry contains 95 suites. The excluded suites require renderer/device or shipping-package execution and were not run as part of this assessment.

## Isolated reruns

```powershell
ctest --test-dir out/dev-fast --output-on-failure --no-tests=error --timeout 60 -j1 -R '^(asset_manifest_editor|content_pipeline)$'
```

Both failed again. See [output](engine-readiness-content-tests.txt). The same command was repeated with `TMP` and `TEMP` pointing to the workspace's `out/engine-readiness-temp` directory; both failures remained. See [workspace-temporary output](engine-readiness-content-workspace-temp-tests.txt).

`asset_manifest_editor` failed six external-audio-import checks. `content_pipeline` threw when the draft missing sound path `audio/missing.ogg` was classified as escaping its content root. Root causes were not established in this assessment.

The [October 5 allocation audit](../../../performance/2026-10-05-frame-allocations/README.md) independently records these same two failures in its 93/95 run and reports that they reproduce with an unchanged earlier Release executable. They predate this assessment.

## Initial Release run

The same 92-suite selection was first run against `out/release`. It passed 84 suites and failed eight. Its older binaries predated later source/content work, so those eight results were not treated as eight current engine defects. The newer optimized-editor run and isolated reruns above are the retained evidence used for the review's release-gate finding.
