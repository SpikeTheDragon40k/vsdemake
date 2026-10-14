/* stb_vorbis (v1.22, public domain / MIT, third_party/stb_vorbis.c) built with
 * this project's options: decode from memory only, the "pull" API, and the
 * plain float-to-int conversion (the fast one depends on byte order and the
 * console is big-endian). */
#define STB_VORBIS_NO_STDIO 1
#define STB_VORBIS_NO_PUSHDATA_API 1
#define STB_VORBIS_NO_FAST_SCALED_FLOAT 1
#if defined(VS_BIG_ENDIAN) && VS_BIG_ENDIAN
#define STB_VORBIS_BIG_ENDIAN 1
#endif
#if defined(_MSC_VER)
#pragma warning(disable: 4244 4245 4701 4100 4456 4457 4996)
#endif
#include "third_party/stb_vorbis.c"
