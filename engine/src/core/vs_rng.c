#include "vs_rng.h"

static uint32_t rotl(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }

void vs_rng_seed(vs_rng *r, uint64_t seed) {
    /* splitmix64 to fill the state; avoids the all-zero state. */
    for (int i = 0; i < 4; i++) {
        seed += 0x9e3779b97f4a7c15ull;
        uint64_t z = seed;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        r->s[i] = (uint32_t)(z ^ (z >> 31));
    }
    if (!(r->s[0] | r->s[1] | r->s[2] | r->s[3])) r->s[0] = 1;
}

uint32_t vs_rng_u32(vs_rng *r) {
    uint32_t *s = r->s;
    uint32_t result = rotl(s[1] * 5, 7) * 9;
    uint32_t t = s[1] << 9;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 11);
    return result;
}

float vs_rng_f32(vs_rng *r) { return (float)(vs_rng_u32(r) >> 8) * (1.0f / 16777216.0f); }

uint32_t vs_rng_below(vs_rng *r, uint32_t n) {
    if (n == 0) return 0;
    uint32_t lim = UINT32_MAX - UINT32_MAX % n;
    uint32_t v;
    do v = vs_rng_u32(r); while (v >= lim);
    return v % n;
}
