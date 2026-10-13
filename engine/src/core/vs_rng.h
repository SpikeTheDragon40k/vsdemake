/* Deterministic RNG owned by the game (never libc rand). xoshiro128**. */
#ifndef VS_RNG_H
#define VS_RNG_H

#include <stdint.h>

typedef struct { uint32_t s[4]; } vs_rng;

void vs_rng_seed(vs_rng *r, uint64_t seed);
uint32_t vs_rng_u32(vs_rng *r);
/* Uniform in [0, 1), like Math.random(). */
float vs_rng_f32(vs_rng *r);
/* Uniform integer in [0, n). */
uint32_t vs_rng_below(vs_rng *r, uint32_t n);

#endif
