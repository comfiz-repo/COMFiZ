#define _POSIX_C_SOURCE 199309L // needed for clock_gettime
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

// Exactly one gadget is compiled at a time, so no two routines are ever timed
// in one process. The build system supplies the directive.
#if (defined(BENCH_SECFXPINV) + defined(BENCH_SECFXPEXP) + defined(BENCH_SECFXPBEREXP) \
   + defined(BENCH_SECFPRINVNR) + defined(BENCH_SECAPPROXEXP) \
   + defined(BENCH_SECFPRBEREXP)) != 1
#error "define exactly one BENCH_ directive"
#endif

// Each tree carries its own gadgets.c and utils.c, so a binary takes one side
// only: linking both would define MaskB, SecAnd, rand64 and the rest twice.
#if defined(BENCH_SECFPRINVNR) || defined(BENCH_SECAPPROXEXP) || defined(BENCH_SECFPRBEREXP)
#define BENCH_SIDE_FPR
#endif

#ifdef BENCH_SIDE_FPR
#include "secfpr.h"
#include "fpr_gadgets.h"
#include "fpr_modify.h"
#else
#include "COMFiZ_gadgets.h"
#endif
#include "gadgets.h"
#include "utils.h"

// Input ranges. x is the reduced exponential argument in [0, ln2), xb the
// BerExp argument over the whole supported interval, sigma Falcon-512's range
// and ccs = sigma_min/sigma. ccsK = ccs/K is the CORDIC initializer.
#define LN2_D       0.6931471805599453
#define SIGMA_MIN_D 1.2778336969128337
#define B_X_D       61.66
#define INV_K_D     1.207497067763072
#define TWO_P54     18014398509481984.0
#define TWO_P62     4611686018427387904.0

// Uniform draw in [0,1) from the top 53 bits of the reference generator.
static inline double unit(void) {
    return (double)(rand64() >> 11) / 9007199254740992.0;
}

static inline uint64_t sigma_bits(void) {
    return (rand64() % 2443951759977325UL) + 0x3FF47201BF2577E7UL;
}

static inline double ccs(uint64_t sigma) {
    double sigma_d;
    memcpy(&sigma_d, &sigma, sizeof(sigma_d));
    return SIGMA_MIN_D / sigma_d;
}

static double elapsed(struct timespec t0, struct timespec t1) {
    return (double)(t1.tv_sec - t0.tv_sec) * 1e6 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e3;
}

// The routine draws one sigma and one argument per iteration, then encodes them in the format the target expects.

#if defined(BENCH_SECFXPINV)
#define BENCH_NAME "SecFxpInv"
static double bench(void) {
    maskedb_t out1, a1;
    struct timespec t0, t1;
    double runs[ITER];
    size_t j;

    for (j = 0; j < ITER; j++) {
        MaskB(a1, sigma_bits());

        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t0);
        SecFxpInv(out1, a1);
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t1);

        runs[j] = elapsed(t0, t1);
    }
    return calc_median(runs);
}

#elif defined(BENCH_SECFPRINVNR)
#define BENCH_NAME "SecFprInvNR"
static double bench(void) {
    maskedb_t out1, a1;
    struct timespec t0, t1;
    double runs[ITER];
    size_t j;

    for (j = 0; j < ITER; j++) {
        MaskB(a1, sigma_bits());

        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t0);
        SecFprInvNR(out1, a1);
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t1);

        runs[j] = elapsed(t0, t1);
    }
    return calc_median(runs);
}

#elif defined(BENCH_SECFXPEXP)
#define BENCH_NAME "SecFxpExp"
static double bench(void) {
    maskedb_t out1, a1, a2;
    struct timespec t0, t1;
    double runs[ITER];
    size_t j;

    for (j = 0; j < ITER; j++) {
        const double ccs_d = ccs(sigma_bits());
        MaskB(a1, (uint64_t)(LN2_D * unit() * TWO_P62));
        MaskB(a2, (uint64_t)(ccs_d * INV_K_D * TWO_P62));

        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t0);
        SecFxpExp(out1, a1, a2);
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t1);

        runs[j] = elapsed(t0, t1);
    }
    return calc_median(runs);
}

#elif defined(BENCH_SECAPPROXEXP)
#define BENCH_NAME "SecApproxExp"
static double bench(void) {
    maskedb_t out1, a1, a2;
    struct timespec t0, t1;
    double runs[ITER];
    size_t j;

    for (j = 0; j < ITER; j++) {
        const double ccs_d = ccs(sigma_bits());
        MaskB(a1, double_to_bits(LN2_D * unit()));
        MaskB(a2, double_to_bits(ccs_d));

        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t0);
        SecApproxExp(out1, a1, a2);
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t1);

        runs[j] = elapsed(t0, t1);
    }
    return calc_median(runs);
}

#elif defined(BENCH_SECFXPBEREXP)
#define BENCH_NAME "SecFxpBerExp"
static double bench(void) {
    maskedb_t out1, a1, a2;
    struct timespec t0, t1;
    double runs[ITER];
    size_t j;

    for (j = 0; j < ITER; j++) {
        const double ccs_d = ccs(sigma_bits());
        MaskB(a1, (uint64_t)(B_X_D * unit() * TWO_P54));
        MaskB(a2, (uint64_t)(ccs_d * INV_K_D * TWO_P62));

        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t0);
        SecFxpBerExp(out1, a1, a2);
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t1);

        runs[j] = elapsed(t0, t1);
    }
    return calc_median(runs);
}

#elif defined(BENCH_SECFPRBEREXP)
#define BENCH_NAME "SecFprBerExp"
static double bench(void) {
    maskedb_t out1, a1, a2, a3;
    struct timespec t0, t1;
    double runs[ITER];
    size_t j;

    for (j = 0; j < ITER; j++) {
        const double ccs_d = ccs(sigma_bits());
        MaskB(a1, double_to_bits(B_X_D * unit()));
        MaskB(a2, double_to_bits(ccs_d));
        MaskB(a3, rand64());

        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t0);
        SecFprBerExp(out1, a1, a2, a3);
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t1);

        runs[j] = elapsed(t0, t1);
    }
    return calc_median(runs);
}
#endif

int main(void) {
    srand((unsigned int)time(NULL));

    printf("  %-14s %9.2fus   (order %u, median of %d runs)\n",
           BENCH_NAME, bench(), (unsigned)MASKORDER, ITER);

    return 0;
}
