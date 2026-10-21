// Correctness checks for the masked gadgets. Each masked result is unmasked and
// compared with a plain, unmasked reference computed here:
//   - the masked BaseSampler against Falcon's RCDT sampler, for the same uniform input
//   - SecFxpExp against CORDIC that updates the residual angle at every step
//   - both polynomial baselines against Falcon's integer ApproxExp
//   - both 128-bit Boolean-to-arithmetic conversions
// Exits with status 1 on the first mismatches.
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "COMFiZ_gadgets.h"
#include "gadgets.h"
#include "utils.h"

// Falcon's RCDT: {bits 71..60, bits 59..0}
static const uint64_t RCDT[18][2] = {
    {0xA3FULL, 0x7F42ED3AC391802ULL}, {0x54DULL, 0x32B181F3F7DDB82ULL},
    {0x227ULL, 0xDCDD0934829C1FFULL}, {0x0ADULL, 0x1754377C7994AE4ULL},
    {0x029ULL, 0x5846CAEF33F1F6FULL}, {0x007ULL, 0x74AC754ED74BD5FULL},
    {0x001ULL, 0x024DD542B776AE4ULL}, {0x000ULL, 117656387352093658ULL},
    {0x000ULL, 8867391802663976ULL},  {0x000ULL, 496969357462633ULL},
    {0x000ULL, 20680885154299ULL},    {0x000ULL, 638331848991ULL},
    {0x000ULL, 14602316184ULL},       {0x000ULL, 247426747ULL},
    {0x000ULL, 3104126ULL},           {0x000ULL, 28824ULL},
    {0x000ULL, 198ULL},               {0x000ULL, 1ULL}
};
static const uint64_t INV_2_SIGMA_MAX_SQ_Q62 = 0x09A7C5E0C1DDE105ULL;

static const uint64_t ATANH_Q62[20] = {
    2533227465661617455ULL, 1177883693488034215ULL, 579491617566063541ULL,
    288606558191708983ULL,  144162128078953545ULL,  72063458959086026ULL,
    36029530053560535ULL,   18014490136289835ULL,   9007210708013329ULL,
    4503601059027081ULL,    2251799992642244ULL,    1125899929212246ULL,
    562949956217515ULL,     281474977060181ULL,     140737488399019ULL,
    70368744183125ULL,      35184372089515ULL,      17592186044501ULL,
    8796093022219ULL,       4398046511105ULL
};

static const uint64_t APPROXEXP_C[13] = {
    0x00000004741183A3ULL, 0x00000036548CFC06ULL, 0x0000024FDCBF140AULL,
    0x0000171D939DE045ULL, 0x0000D00CF58F6F84ULL, 0x000680681CF796E3ULL,
    0x002D82D8305B0FEAULL, 0x011111110E066FD0ULL, 0x0555555555070F00ULL,
    0x155555555581FF00ULL, 0x400000000002B400ULL, 0x7FFFFFFFFFFF4800ULL,
    0x8000000000000000ULL
};

#define HALF36 ((1ULL << 36) - 1ULL)
#define LN2_Q62 0x2C5C85FDF473DE6BULL

static unsigned long long checks, failures;

static void check(int ok, const char *what, unsigned long long trial) {
    checks++;
    if (!ok) {
        if (failures < 10) printf("  FAIL %s (trial %llu)\n", what, trial);
        failures++;
    }
}

// Sum of arithmetic shares mod 2^128
static void unmask_a128(uint64_t *hi, uint64_t *lo, maskeda_t ah, maskeda_t al) {
    size_t j;
    *hi = 0;
    *lo = 0;
    for (j = 0; j < MASKSIZE; j++) Add128(hi, lo, *hi, *lo, ah[j], al[j]);
}

static uint64_t unmask_b(maskedb_t x) {
    uint64_t v;
    UnmaskB(&v, x);
    return v;
}

// ---------------------------------------------------------------------------
// BaseSampler

static void ref_base_sampler(uint64_t *z, uint64_t *ch, uint64_t *cl,
                             uint64_t u_hi, uint64_t u_lo, uint64_t beta) {
    // u = u_hi * 2^36 + u_lo, split as in RCDT
    const uint64_t uh = u_hi >> 24;
    const uint64_t ul = ((u_hi & ((1ULL << 24) - 1ULL)) << 36) | u_lo;
    uint64_t z0 = 0, ph, pl;
    int i;

    for (i = 0; i < 18; i++)
        z0 += (uint64_t)(uh < RCDT[i][0] || (uh == RCDT[i][0] && ul < RCDT[i][1]));
    *z = beta ? z0 + 1ULL : 0ULL - z0;
    Mult128(&ph, &pl, z0 * z0, INV_2_SIGMA_MAX_SQ_Q62);
    *ch = (ph << 47) ^ (pl >> 17);
    *cl = pl << 47;
}

static void test_base_sampler_at(uint64_t u_hi, uint64_t u_lo, uint64_t beta, unsigned long long k) {
    maskedb_t mh, ml, mb, z;
    maskeda_t ch, cl;
    uint64_t rz, rch, rcl, gh, gl;

    ref_base_sampler(&rz, &rch, &rcl, u_hi, u_lo, beta);
    MaskB(mh, u_hi);
    MaskB(ml, u_lo);
    MaskB(mb, beta);
    SecBaseSamplerFromUniform(z, ch, cl, mh, ml, mb);
    unmask_a128(&gh, &gl, ch, cl);
    check(unmask_b(z) == rz, "BaseSampler z", k);
    check(gh == rch && gl == rcl, "BaseSampler C(z0)", k);
}

static void test_base_sampler(unsigned long long n) {
    unsigned long long k;
    int i;

    // every threshold and its predecessor, both signs
    for (i = 0; i < 18; i++) {
        const uint64_t hi = (RCDT[i][0] << 24) | (RCDT[i][1] >> 36);
        const uint64_t lo = RCDT[i][1] & HALF36;
        const uint64_t hm = lo ? hi : hi - 1ULL;
        const uint64_t lm = (lo - 1ULL) & HALF36;
        test_base_sampler_at(hi, lo, 0, (unsigned long long)i);
        test_base_sampler_at(hi, lo, 1, (unsigned long long)i);
        test_base_sampler_at(hm, lm, 0, (unsigned long long)i);
        test_base_sampler_at(hm, lm, 1, (unsigned long long)i);
    }
    test_base_sampler_at(HALF36, HALF36, 1, 0);
    test_base_sampler_at(0, 0, 0, 0);

    for (k = 0; k < n; k++)
        test_base_sampler_at(rand64() >> 28, rand64() & HALF36, rand64() & 1ULL, k);
}

// ---------------------------------------------------------------------------
// CORDIC exponential: the residual angle is updated at every step

static uint64_t ref_cordic_exp(uint64_t x_q62, uint64_t ccsK_q62) {
    uint64_t w = ccsK_q62, z = x_q62;
    int i = 1, repeat = 0;

    while (i <= 60) {
        const uint64_t angle = i <= 20 ? ATANH_Q62[i - 1] : 1ULL << (62 - i);
        const uint64_t ws = w >> i;
        if ((z >> 63) == 0) {
            z -= angle;
            w -= ws;
        } else {
            z += angle;
            w += ws;
        }
        if ((i == 4 || i == 13 || i == 40) && !repeat) repeat = 1;
        else { i++; repeat = 0; }
    }
    return w;
}

static void test_cordic_exp(unsigned long long n) {
    // claimed range: x_q62 in [-2^60, ln2 * 2^62 + 2^60]
    const uint64_t lo = 0ULL - (1ULL << 60);
    const uint64_t span = LN2_Q62 + (2ULL << 60);
    unsigned long long k;

    for (k = 0; k < n + 3; k++) {
        maskedb_t mx, mc, out;
        // ccs/K with ccs in [sigma_min / sigma_max, 1]
        const uint64_t c = 0x363E66737CC1E800ULL + rand64() % 0x17093B5486F92000ULL;
        uint64_t x;

        if (k == n) x = lo;
        else if (k == n + 1) x = lo + span;
        else if (k == n + 2) x = 0;
        else x = lo + rand64() % (span + 1ULL);

        MaskB(mx, x);
        MaskB(mc, c);
        SecFxpExp(out, mx, mc);
        check(unmask_b(out) == ref_cordic_exp(x, c), "SecFxpExp", k);
    }
}

// ---------------------------------------------------------------------------
// Falcon's integer ApproxExp: y = C[u] - floor(z * y / 2^63) with z = x in Q63,
// then ccs * y / 2^64, i.e. ccs * exp(-x) in Q62

static uint64_t ref_poly_exp(uint64_t x_q62, uint64_t ccs_q63) {
    const uint64_t z = x_q62 << 1;
    uint64_t y = APPROXEXP_C[0], ph, pl;
    int u;

    for (u = 1; u <= 12; u++) {
        Mult128(&ph, &pl, z, y);
        y = APPROXEXP_C[u] - ((ph << 1) | (pl >> 63));
    }
    Mult128(&ph, &pl, ccs_q63, y);
    return ph;
}

static void test_poly_exp(unsigned long long n) {
    unsigned long long k;

    for (k = 0; k < n; k++) {
        maskedb_t mx, mc, out;
        const uint64_t x = k == 0 ? 0 : (k == 1 ? LN2_Q62 - 1ULL : rand64() % LN2_Q62);
        // ccs in [sigma_min / sigma_max, 1), Q63
        const uint64_t c = 0x59D84DFC495C9000ULL + rand64() % 0x2627B203B6A37000ULL;
        const uint64_t ref = ref_poly_exp(x, c);

        MaskB(mx, x);
        MaskB(mc, c);
        SecFxpPolyExp(out, mx, mc);
        check(unmask_b(out) == ref, "SecFxpPolyExp (CHES 2017 B2A)", k);

        MaskB(mx, x);
        MaskB(mc, c);
        SecFxpPolyExpQuad(out, mx, mc);
        check(unmask_b(out) == ref, "SecFxpPolyExpQuad (CHES 2014 B2A)", k);
    }
}

// ---------------------------------------------------------------------------
// Boolean to arithmetic, mod 2^128

static void test_b2a(unsigned long long n) {
    unsigned long long k;

    for (k = 0; k < n; k++) {
        maskedb_t bh, bl;
        maskeda_t ah, al;
        const uint64_t h = rand64(), l = rand64();
        uint64_t gh, gl;

        MaskB(bh, h);
        MaskB(bl, l);
        B2A128(ah, al, bh, bl, MASKSIZE);
        unmask_a128(&gh, &gl, ah, al);
        check(gh == h && gl == l, "B2A128", k);

        MaskB(bh, h);
        MaskB(bl, l);
        B2A128Quad(ah, al, bh, bl, MASKSIZE);
        unmask_a128(&gh, &gl, ah, al);
        check(gh == h && gl == l, "B2A128Quad", k);
    }
}

int main(void) {
    // fewer trials as the masking order grows; the CHES 2017 B2A doubles per order
    const unsigned long long n = 20000ULL / (MASKSIZE * MASKSIZE) + 100ULL;
    const unsigned long long n_poly = MASKORDER > 6 ? n / (1ULL << (MASKORDER - 6)) + 20ULL : n;

    test_base_sampler(n);
    test_cordic_exp(n);
    test_poly_exp(n_poly);
    test_b2a(n_poly);

    if (failures) {
        printf("test_comfiz order %u: %llu of %llu checks FAILED\n",
               (unsigned)MASKORDER, failures, checks);
        return EXIT_FAILURE;
    }
    printf("test_comfiz order %u: all %llu checks passed\n", (unsigned)MASKORDER, checks);
    return EXIT_SUCCESS;
}
