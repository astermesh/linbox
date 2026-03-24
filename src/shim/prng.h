#ifndef LINBOX_SHIM_PRNG_H
#define LINBOX_SHIM_PRNG_H

#include <stddef.h>
#include <stdint.h>

typedef struct linbox_prng {
    uint64_t state;
} linbox_prng_t;

void linbox_prng_seed(linbox_prng_t *prng, uint64_t seed);
uint64_t linbox_prng_next_u64(linbox_prng_t *prng);
uint32_t linbox_prng_next_u32(linbox_prng_t *prng);
void linbox_prng_fill(linbox_prng_t *prng, void *buf, size_t len);
uint32_t linbox_prng_uniform(linbox_prng_t *prng, uint32_t upper_bound);

#endif
