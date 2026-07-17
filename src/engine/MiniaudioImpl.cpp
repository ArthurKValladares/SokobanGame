// miniaudio implementation translation unit. OGG Vorbis decoding comes from
// the bundled stb_vorbis, which must sandwich the miniaudio implementation
// exactly as documented by miniaudio.
#define STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c" /* Enables OGG Vorbis decoding. */

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

/* stb_vorbis implementation must come after the miniaudio implementation. */
#undef STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
