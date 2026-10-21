#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <string.h>

// Timed runs per benchmark binary; set it with cmake -DITER=100000.
#ifndef ITER
#define ITER 10000
#endif

// ChaCha20 (Falcon's reference PRNG), seeded from /dev/urandom.
uint64_t rand64(void);
uint64_t randmod(uint64_t mod);

uint64_t double_to_bits(double d);
double calc_median(double values[ITER]);
int cmp_double(const void *a, const void *b);
void print_binary_form(uint64_t in);

// For 128-bit integer, built from 32x32 -> 64 multiplications

void     Mult128    (uint64_t *out1, uint64_t *out2, uint64_t in1, uint64_t in2);
void     Add128     (uint64_t *out1, uint64_t *out2, uint64_t in11, uint64_t in12, uint64_t in21, uint64_t in22);
void     Mult128Bi  (uint64_t *outup, uint64_t *outdown, uint64_t in1up, uint64_t in1down, uint64_t in2up, uint64_t in2down);

// modular op;

uint64_t subq(uint64_t ina, uint64_t inb, uint64_t mod);
#endif
