# stb_image

This directory vendors `stb_image.h` version 2.30 from the official
[`nothings/stb`](https://github.com/nothings/stb) repository. The game uses
the memory-decoding API so filesystem access remains owned by the engine and
works with `std::filesystem::path` on every supported platform.

The upstream dual-license text is in `LICENSE`.
