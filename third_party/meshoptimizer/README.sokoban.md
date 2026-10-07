# Vendored meshoptimizer

Upstream: https://github.com/zeux/meshoptimizer

Release: **v1.3**, commit `9e1f07b159d3cb777f1c67ed31fc11fd117986f4`.

License: MIT, reproduced verbatim in `LICENSE.md` and `meshoptimizer.h`.
Copyright (c) 2016-2026 Arseny Kapoulkine.

This is the unchanged upstream subset used for exact vertex remapping,
vertex-cache ordering, vertex-fetch ordering, and cache/fetch analysis. It
excludes the upstream simplifier, remesher, codec, and tool implementation units
and build scripts. Additional declarations and inline helpers remain in the
unmodified upstream header; not all declared algorithms are linked.
The project's CMake target owns the source list; no download occurs during a
normal configure or build.

Files were downloaded from `https://raw.githubusercontent.com/zeux/meshoptimizer/v1.3/src/`
(the license is from the release root). To update, select an official release,
replace this same subset and license, update the pinned commit and hashes, and
run the lossless invariant tests before changing the processed-geometry compiler
revision.

SHA-256 checksums of the unmodified upstream files:

| File | SHA-256 |
| --- | --- |
| allocator.cpp | `d2cc48691fe2f4c6d097bf7a766389cfb7f83ca14a5f747e3944332656d02254` |
| indexanalyzer.cpp | `bff37aecb10cefa33f3f6a217413c6ac98899c23e6de5282420c60e6baf786ec` |
| indexgenerator.cpp | `f362cdcd8af10016333bf699347a9364335987f413f061e5b7e8cafe3b498948` |
| LICENSE.md | `f03037ca7bad1e3eb7f4a63fa6084a8baabd5ba30d3c239a9a7f35705d873e26` |
| meshoptimizer.h | `e3688ef553f603391747895a5c221ef1528574cc7f8173782417a96cb531f61b` |
| vcacheoptimizer.cpp | `618429ef4db8ab9b16fde4e73dcf425d89452acbf103085478bc8b1761fa6689` |
| vfetchoptimizer.cpp | `aa534bb8150ca27ae229c58a0dc51f85f9acccbc5d4214f4c82edd91fc08e478` |
