#define _POSIX_C_SOURCE 199309L // needed for clock_gettime
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

// Exactly one sampler is compiled at a time, so no two are ever timed in one
// process. BPTBC24_NR additionally selects the Newton-Raphson reciprocal inside SamplerZ.
#if (defined(SAMPLER_COMFIZ) + defined(SAMPLER_BPTBC24)) != 1
#error "define exactly one SAMPLER_ directive"
#endif

#ifdef SAMPLER_COMFIZ
#include "COMFiZ.h"
#define SAMPLER_NAME "COMFiZ"
#define SAMPLER_RUN  COMFiZ
#else
#include "secfpr.h"
#define SAMPLER_RUN  SamplerZ
#ifdef BPTBC24_NR
#define SAMPLER_NAME "BPTBC24+NR"
#else
#define SAMPLER_NAME "BPTBC24"
#endif
#endif
#include "gadgets.h"
#include "utils.h"

static double elapsed(struct timespec t0, struct timespec t1) {
    return (double)(t1.tv_sec - t0.tv_sec) * 1e6 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e3;
}

int main(void) {
    maskedb_t in1, in2, out1;
    struct timespec t0, t1;

    double runs[ITER];
    size_t j;

    srand((unsigned int)time(NULL));

    for (j = 0; j < ITER; j++){
        MaskB(in1, (rand64()%0x40C3880000000000));
        MaskB(in2, ((rand64()%2443951759977325) + 0x3FF47201BF2577E7));

        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t0);

        SAMPLER_RUN(out1, in1, in2);

        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t1);

        runs[j] = elapsed(t0, t1);
    }
    printf("  %-14s %9.2fus   (order %u, median of %d runs)\n",
           SAMPLER_NAME, calc_median(runs), (unsigned)MASKORDER, ITER);

    return 0;
}
