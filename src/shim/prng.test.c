#include <criterion/criterion.h>

#include <string.h>

#include "prng.h"

Test(prng, same_seed_same_bytes) {
    linbox_prng_t a = {0};
    linbox_prng_t b = {0};
    unsigned char out_a[32];
    unsigned char out_b[32];

    linbox_prng_seed(&a, 1234);
    linbox_prng_seed(&b, 1234);
    linbox_prng_fill(&a, out_a, sizeof(out_a));
    linbox_prng_fill(&b, out_b, sizeof(out_b));

    cr_assert_eq(memcmp(out_a, out_b, sizeof(out_a)), 0);
}

Test(prng, different_seed_different_bytes) {
    linbox_prng_t a = {0};
    linbox_prng_t b = {0};
    unsigned char out_a[32];
    unsigned char out_b[32];

    linbox_prng_seed(&a, 111);
    linbox_prng_seed(&b, 222);
    linbox_prng_fill(&a, out_a, sizeof(out_a));
    linbox_prng_fill(&b, out_b, sizeof(out_b));

    cr_assert_neq(memcmp(out_a, out_b, sizeof(out_a)), 0);
}

Test(prng, uniform_within_range) {
    linbox_prng_t p = {0};
    linbox_prng_seed(&p, 777);

    for (int i = 0; i < 1000; i++) {
        uint32_t v = linbox_prng_uniform(&p, 100);
        cr_assert_lt(v, 100u);
    }
}
