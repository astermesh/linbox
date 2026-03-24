#include "prng.h"

#include <stdint.h>

static uint64_t splitmix64(uint64_t *x) {
    *x += 0x9E3779B97F4A7C15ULL;
    uint64_t z = *x;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void linbox_prng_seed(linbox_prng_t *prng, uint64_t seed) {
    if (!prng) {
        return;
    }

    uint64_t s = seed;
    prng->state = splitmix64(&s);
    if (prng->state == 0) {
        prng->state = 0xA5A5A5A5A5A5A5A5ULL;
    }
}

uint64_t linbox_prng_derive_process_seed(uint64_t seed, uint32_t pid) {
    uint64_t mixed = seed ^ (((uint64_t)pid << 32) | (uint64_t)pid);
    return splitmix64(&mixed);
}

uint64_t linbox_prng_next_u64(linbox_prng_t *prng) {
    uint64_t x = prng->state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    prng->state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

uint32_t linbox_prng_next_u32(linbox_prng_t *prng) {
    return (uint32_t)(linbox_prng_next_u64(prng) >> 32);
}

void linbox_prng_fill(linbox_prng_t *prng, void *buf, size_t len) {
    uint8_t *out = (uint8_t *)buf;
    size_t offset = 0;

    while (offset < len) {
        uint64_t word = linbox_prng_next_u64(prng);
        for (size_t i = 0; i < sizeof(word) && offset < len; i++, offset++) {
            out[offset] = (uint8_t)(word & 0xFFu);
            word >>= 8;
        }
    }
}

uint32_t linbox_prng_uniform(linbox_prng_t *prng, uint32_t upper_bound) {
    if (upper_bound == 0) {
        return 0;
    }

    uint32_t threshold = (uint32_t)(-upper_bound) % upper_bound;
    for (;;) {
        uint32_t r = linbox_prng_next_u32(prng);
        if (r >= threshold) {
            return r % upper_bound;
        }
    }
}
