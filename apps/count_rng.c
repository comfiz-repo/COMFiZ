// Counts the rand64() calls made inside one SamplerZ call. Built by
// run_count_rng.sh with -DCOUNT_RNG; the masking of the inputs is not counted.
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gadgets.h"
#include "utils.h"

#if (defined(SAMPLER_COMFIZ) + defined(SAMPLER_BPTBC24)) != 1
#error "Define exactly one of SAMPLER_COMFIZ and SAMPLER_BPTBC24"
#endif

#ifdef SAMPLER_COMFIZ
#include "COMFiZ.h"
#define RUN COMFiZ
#define NAME "COMFiZ"
#else
#include "secfpr.h"
#define RUN SamplerZ
#ifdef BPTBC24_NR
#define NAME "BPTBC24+NR"
#else
#define NAME "BPTBC24"
#endif
#endif

#ifndef COUNT_RNG
#error "Build this counter with -DCOUNT_RNG"
#endif

extern uint64_t rand64_calls;

static uint64_t bits(double value) {
    uint64_t encoded;
    memcpy(&encoded, &value, sizeof(encoded));
    return encoded;
}

int main(int argc, char **argv) {
    static const double centers[] = {0.0, 0.25, 0.5, 0.75};
    static const double widths[] = {1.2778336969128337, 1.4, 1.6, 1.8205};
    uint64_t runs = 1000, total = 0, minimum = UINT64_MAX, maximum = 0;
    uint64_t k;
    double mean;
    int csv = argc == 3 && strcmp(argv[2], "--csv") == 0;

    if (argc > 3 || (argc == 3 && !csv)) goto usage;
    if (argc > 1) {
        char *end;
        unsigned long long parsed;
        if (argv[1][0] < '1' || argv[1][0] > '9') goto usage;
        errno = 0;
        parsed = strtoull(argv[1], &end, 10);
        if (errno || *end || parsed > UINT64_MAX) goto usage;
        runs = (uint64_t)parsed;
    }
    if (sizeof(double) != sizeof(uint64_t)) {
        fprintf(stderr, "This sampler interface requires binary64 doubles.\n");
        return EXIT_FAILURE;
    }

    rand64_calls = 0;
    (void)rand64();
    if (rand64_calls != 1) {
        fprintf(stderr, "Counter inactive: rand64() must increment rand64_calls "
                        "under COUNT_RNG.\n");
        return EXIT_FAILURE;
    }
    rand64_calls = 0;
    (void)randmod(UINT64_C(1) << 32);
    if (rand64_calls != 1) {
        fprintf(stderr, "randmod must draw through rand64. Check the RNG "
                        "in both utils.c implementations.\n");
        return EXIT_FAILURE;
    }

    for (k = 0; k < runs; k++) {
        maskedb_t mu, sigma, out;
        MaskB(mu, bits(centers[k % 4]));
        MaskB(sigma, bits(widths[(k / 4) % 4]));
        rand64_calls = 0;
        RUN(out, mu, sigma);
        if (UINT64_MAX - total < rand64_calls) {
            fprintf(stderr, "rand64() call total overflow.\n");
            return EXIT_FAILURE;
        }
        total += rand64_calls;
        if (rand64_calls < minimum) minimum = rand64_calls;
        if (rand64_calls > maximum) maximum = rand64_calls;
    }
    mean = (double)total / (double)runs;
    if (csv) {
        printf("%s,%u,%" PRIu64 ",%" PRIu64 ",%.6f,%" PRIu64 ",%" PRIu64 "\n",
               NAME, (unsigned)MASKORDER, runs, total, mean, minimum, maximum);
    } else {
        printf("%s | order %u | %" PRIu64 " completed samples\n",
               NAME, (unsigned)MASKORDER, runs);
        printf("Total rand64() calls: %" PRIu64 "\n", total);
        printf("Mean rand64() calls per sample: %.2f\n", mean);
        printf("Min/max calls per sample: %" PRIu64 " / %" PRIu64 "\n", minimum, maximum);
    }
    return EXIT_SUCCESS;

usage:
    fprintf(stderr, "Usage: %s [positive sample count] [--csv]\n", argv[0]);
    return EXIT_FAILURE;
}
