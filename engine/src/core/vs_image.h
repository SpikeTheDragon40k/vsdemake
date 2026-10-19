/* JPEG decoding (stb_image) for the images built into the executable. */
#ifndef VS_IMAGE_H
#define VS_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "core/vs_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decodes a JPEG in memory to w × h RGBA bytes (free with vs_image_free); NULL on error. */
uint8_t *vs_image_decode(const uint8_t *data, size_t size, int *w, int *h);
void vs_image_free(uint8_t *pixels);

#ifdef __cplusplus
}
#endif

#endif
