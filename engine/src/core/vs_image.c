/* stb_image (v2.30, public domain / MIT, third_party/stb_image.h) built with
 * this project's options: baseline JPEG from memory only, no SIMD (the
 * console is PowerPC). */
#include "core/vs_image.h"

#define STBI_ONLY_JPEG 1
#define STBI_NO_STDIO 1
#define STBI_NO_LINEAR 1
#define STBI_NO_HDR 1
#define STBI_NO_SIMD 1
#define STB_IMAGE_IMPLEMENTATION 1
#if defined(_MSC_VER)
#pragma warning(disable: 4244 4245 4701 4100 4456 4457 4996 4127)
#endif
#include "third_party/stb_image.h"

uint8_t *vs_image_decode(const uint8_t *data, size_t size, int *w, int *h) {
    int n;
    if (size > 0x7fffffff) return NULL;
    return stbi_load_from_memory(data, (int)size, w, h, &n, 4);
}

void vs_image_free(uint8_t *pixels) { stbi_image_free(pixels); }
