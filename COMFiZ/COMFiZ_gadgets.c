#include <stddef.h>
#include <stdint.h>

#include "COMFiZ_gadgets.h"
#include "gadgets.h"
#include "utils.h"

// =============================================================================
// Constants & Lookup Tables
// =============================================================================

static const uint64_t LN2_Q62                = 0x2C5C85FDF473DE6BULL;
static const uint64_t INV_LN2_Q56             = 0x0171547652B82FE1ULL;

static const uint64_t atanh_table[20] = {
    2533227465661617455ULL, 1177883693488034215ULL,
    579491617566063541ULL,  288606558191708983ULL,
    144162128078953545ULL,  72063458959086026ULL,
    36029530053560535ULL,   18014490136289835ULL,
    9007210708013329ULL,    4503601059027081ULL,
    2251799992642244ULL,    1125899929212246ULL,
    562949956217515ULL,     281474977060181ULL,
    140737488399019ULL,     70368744183125ULL,
    35184372089515ULL,      17592186044501ULL,
    8796093022219ULL,       4398046511105ULL
};

// BaseSampler: lane i of BASE_RCDT_PLANES[b] is bit b of Falcon's 72-bit RCDT[i].
static const uint64_t BASE_RCDT_PLANES[72] = {
    0x22E34ULL, 0x16CB7ULL, 0x1487CULL, 0x0FFB4ULL, 0x0FCA4ULL, 0x0675CULL,
    0x146FCULL, 0x1B4CEULL, 0x05CB6ULL, 0x020DAULL, 0x06330ULL, 0x07A7BULL,
    0x0C433ULL, 0x0B5E0ULL, 0x0E1CEULL, 0x01026ULL, 0x079DFULL, 0x06950ULL,
    0x072F2ULL, 0x0799FULL, 0x0357BULL, 0x061F7ULL, 0x01562ULL, 0x02C88ULL,
    0x008FAULL, 0x03B5AULL, 0x03CABULL, 0x02967ULL, 0x00A12ULL, 0x016F3ULL,
    0x010BCULL, 0x00B39ULL, 0x0160FULL, 0x0141FULL, 0x00EFAULL, 0x00792ULL,
    0x00BEFULL, 0x00218ULL, 0x007E9ULL, 0x00F95ULL, 0x002E8ULL, 0x006A9ULL,
    0x000F1ULL, 0x000D3ULL, 0x004C6ULL, 0x00091ULL, 0x002FCULL, 0x003E4ULL,
    0x0038EULL, 0x00122ULL, 0x0015DULL, 0x00126ULL, 0x00109ULL, 0x000CBULL,
    0x0002DULL, 0x00095ULL, 0x000BFULL, 0x00023ULL, 0x00035ULL, 0x00004ULL,
    0x0007FULL, 0x00025ULL, 0x0002FULL, 0x0001BULL, 0x00001ULL, 0x0001DULL,
    0x00002ULL, 0x00008ULL, 0x00002ULL, 0x00005ULL, 0x00002ULL, 0x00001ULL,
};

// BaseSampler: {hi, lo} of C(k) ^ C(k+1), C(k) = k*k*0x09A7C5E0C1DDE105*2^47 mod 2^128.
static const uint64_t BASE_CORR_DELTA[18][2] = {
    {0x000004D3E2F060EEULL, 0xF082800000000000ULL},
    {0x0000179C6931E355ULL, 0x3288800000000000ULL},
    {0x0000383D71B2EBDDULL, 0xB69C800000000000ULL},
    {0x0000664CD5756689ULL, 0x7CBE800000000000ULL},
    {0x0000358F067F79BAULL, 0x7496800000000000ULL},
    {0x0000D57AC0B4D6CCULL, 0xAEE4800000000000ULL},
    {0x0000414599CF2C25ULL, 0xDAA0800000000000ULL},
    {0x0001D876CC1AB600ULL, 0x285A800000000000ULL},
    {0x0000B3F272169026ULL, 0x39EA800000000000ULL},
    {0x000065CE6BEB76CFULL, 0xEBB0800000000000ULL},
    {0x0003AAE2E6780DBAULL, 0x5F54800000000000ULL},
    {0x0000FF09E4AB5688ULL, 0xE4C6800000000000ULL},
    {0x000198CF77997BDBULL, 0x8F4E800000000000ULL},
    {0x00009DD910A5CB4CULL, 0xE5CC800000000000ULL},
    {0x00078C03B54F04F1ULL, 0x4158800000000000ULL},
    {0x0000EDD88525DCF1ULL, 0xE032800000000000ULL},
    {0x0001A0D1C13D834DULL, 0x01D2800000000000ULL},
    {0x00036F180967C3D5ULL, 0xE678800000000000ULL},
};

// Falcon's ApproxExp coefficients (Q63), in Horner order, for the polynomial baseline.
static const uint64_t APPROXEXP_C[13] = {
    0x00000004741183A3ULL, 0x00000036548CFC06ULL, 0x0000024FDCBF140AULL,
    0x0000171D939DE045ULL, 0x0000D00CF58F6F84ULL, 0x000680681CF796E3ULL,
    0x002D82D8305B0FEAULL, 0x011111110E066FD0ULL, 0x0555555555070F00ULL,
    0x155555555581FF00ULL, 0x400000000002B400ULL, 0x7FFFFFFFFFFF4800ULL,
    0x8000000000000000ULL
};

static uint64_t CORDICAtanhQ62(unsigned i) {
    if (i <= 20) return atanh_table[i - 1];
    return 1ULL << (62 - i);
}

// =============================================================================
// Share-Local and Boolean Gadgets
// =============================================================================

void SecNeg(maskedb_t out, maskedb_t in, unsigned pos) {
    size_t j;
    for (j = 0; j < MASKSIZE; j++) out[j] = 0ULL - ((in[j] >> pos) & 1ULL);
    RefreshXOR_64(out, out, MASKSIZE);
}

void SecMux(maskedb_t out, maskedb_t x, maskedb_t y, maskedb_t cond) {
    size_t j;
    maskedb_t mask, term;

    for (j = 0; j < MASKSIZE; j++) {
        mask[j] = 0ULL - (cond[j] & 1ULL);
    }
    SecAnd(term, mask, y, MASKSIZE);
    for (j = 0; j < MASKSIZE; j++) {
        out[j] = x[j] ^ term[j];
    }
    RefreshXOR_64(out, out, MASKSIZE);
}

void SecLessThan(maskedb_t out, maskedb_t a, maskedb_t b) {
    uint64_t p[MASKSIZE], g[MASKSIZE], t[MASKSIZE], t2[MASKSIZE];
    maskedb_t nb;
    size_t i, j;
    int pow = 1;
    size_t log2km1 = 6;

    for (i = 0; i < MASKSIZE; i++) nb[i] = b[i];
    nb[0] = ~nb[0];

    for (i = 0; i < MASKSIZE; i++) p[i] = a[i] ^ nb[i];
    SecAnd(g, a, nb, MASKSIZE);

    for (i = 0; i < MASKSIZE; i++) g[i] ^= p[i] & 1ULL;

    for (j = 0; j < log2km1 - 1; j++) {
        for (i = 0; i < MASKSIZE; i++) t[i] = g[i] << pow;
        SecAnd(t2, t, p, MASKSIZE);
        for (i = 0; i < MASKSIZE; i++) {
            g[i] ^= t2[i];
            t2[i] = p[i] << pow;
        }
        RefreshXOR_64(t2, t2, MASKSIZE);
        SecAnd(t, p, t2, MASKSIZE);
        for (i = 0; i < MASKSIZE; i++) p[i] = t[i];
        pow *= 2;
    }

    for (i = 0; i < MASKSIZE; i++) t[i] = g[i] << pow;
    SecAnd(t2, t, p, MASKSIZE);
    for (i = 0; i < MASKSIZE; i++) g[i] ^= t2[i];

    for (i = 0; i < MASKSIZE; i++) out[i] = g[i] >> 63;
    out[0] ^= 1ULL;
    RefreshXOR_64(out, out, MASKSIZE);
}

void SecVarShift(maskedb_t reg, maskedb_t shift) {
    size_t j;
    unsigned k;
    maskedb_t cond, delta;

    for (k = 0; k < 6; k++) {
        const unsigned n = 1U << k;
        SecNeg(cond, shift, k);
        for (j = 0; j < MASKSIZE; j++) {
            delta[j] = reg[j] ^ (reg[j] >> n);
        }
        SecMux(reg, reg, delta, cond);
    }
}

static void RotateRight128(uint64_t *out_h, uint64_t *out_l, uint64_t in_h, uint64_t in_l, unsigned rot) {
    uint64_t h, l;

    rot &= 127U;
    if (rot == 0) {
        h = in_h;
        l = in_l;
    } else if (rot < 64) {
        h = (in_h >> rot) | (in_l << (64U - rot));
        l = (in_l >> rot) | (in_h << (64U - rot));
    } else if (rot == 64) {
        h = in_l;
        l = in_h;
    } else {
        const unsigned r = rot - 64U;
        h = (in_l >> r) | (in_h << (64U - r));
        l = (in_h >> r) | (in_l << (64U - r));
    }

    *out_h = h;
    *out_l = l;
}

void SecFprIrsh128(maskedb_t out_bh, maskedb_t out_bl, maskedb_t in_bh, maskedb_t in_bl, maskeda_t c_a) {
    maskedb_t x_bh, x_bl, m_bh, m_bl, sign;
    maskedb_t y_bh, y_bl, t_bh, t_bl, nm_bh, nm_bl;
    size_t i, j;
    unsigned len;

    for (j = 0; j < MASKSIZE; j++) {
        x_bh[j] = in_bh[j];
        x_bl[j] = in_bl[j];
        m_bh[j] = 0;
        m_bl[j] = 0;
    }
    m_bh[0] = 1ULL << 63;

    for (j = 0; j < MASKSIZE; j++) sign[j] = 0ULL - ((in_bh[j] >> 63) & 1ULL);

    for (i = 0; i < MASKSIZE; i++) {
        const unsigned rot = (unsigned)(c_a[i] & 127ULL);

        for (j = 0; j < MASKSIZE; j++) {
            RotateRight128(&x_bh[j], &x_bl[j], x_bh[j], x_bl[j], rot);
            RotateRight128(&m_bh[j], &m_bl[j], m_bh[j], m_bl[j], rot);
        }
        RefreshXOR_64(x_bh, x_bh, MASKSIZE);
        RefreshXOR_64(x_bl, x_bl, MASKSIZE);
        RefreshXOR_64(m_bh, m_bh, MASKSIZE);
        RefreshXOR_64(m_bl, m_bl, MASKSIZE);
    }

    for (len = 1; len <= 32; len <<= 1) {
        for (j = 0; j < MASKSIZE; j++) {
            m_bl[j] ^= (m_bl[j] >> len) ^ (m_bh[j] << (64U - len));
            m_bh[j] ^= m_bh[j] >> len;
        }
    }
    for (j = 0; j < MASKSIZE; j++) m_bl[j] ^= m_bh[j];

    SecAnd(y_bh, x_bh, m_bh, MASKSIZE);
    SecAnd(y_bl, x_bl, m_bl, MASKSIZE);

    for (j = 0; j < MASKSIZE; j++) {
        nm_bh[j] = m_bh[j];
        nm_bl[j] = m_bl[j];
    }
    nm_bh[0] = ~nm_bh[0];
    nm_bl[0] = ~nm_bl[0];

    SecAnd(t_bh, nm_bh, sign, MASKSIZE);
    SecAnd(t_bl, nm_bl, sign, MASKSIZE);

    SecOr(out_bh, y_bh, t_bh, MASKSIZE);
    SecOr(out_bl, y_bl, t_bl, MASKSIZE);
}

void SecFprToInt128(maskedb_t mu_floor, maskedb_t mu_frac_q54, maskedb_t mu_fpr) {
    maskedb_t m, e, e_bh, sign_bit, sign_mask, value_sign;
    maskedb_t q_bh, q_bl, zero, sat_bh, sat_bl, sat_bit, dif_bh, dif_bl;
    maskeda_t e_ah, e_al, c_ah, c_al, d_ah, d_al;
    size_t j;

    for (j = 0; j < MASKSIZE; j++) {
        m[j] = (mu_fpr[j] & 0x000FFFFFFFFFFFFFULL) << 10;
        e[j] = (mu_fpr[j] >> 52) & 0x7FFULL;
        e_bh[j] = 0;
    }
    SecNeg(sign_mask, mu_fpr, 63);

    m[0] ^= 1ULL << 62;

    B2A128(e_ah, e_al, e_bh, e, MASKSIZE);
    for (j = 0; j < MASKSIZE; j++) {
        uint64_t c_l = 0ULL - e_al[j];
        uint64_t c_h = ~e_ah[j] + (uint64_t)(e_al[j] == 0);

        if (j == 0) {
            const uint64_t s = c_l + 1095ULL;
            c_h += (uint64_t)(s < c_l);
            c_l = s;
        }
        c_ah[j] = c_h;
        c_al[j] = c_l;
    }

    for (j = 0; j < MASKSIZE; j++) {
        m[j] ^= sign_mask[j];
        sign_bit[j] = sign_mask[j] & 1ULL;
    }
    RefreshXOR_64(sign_bit, sign_bit, MASKSIZE);
    SecAdd(m, m, sign_bit, MASKSIZE);

    SecNeg(value_sign, m, 63);

    for (j = 0; j < MASKSIZE; j++) zero[j] = 0;
    SecFprIrsh128(q_bh, q_bl, m, zero, c_al);

    for (j = 0; j < MASKSIZE; j++) {
        uint64_t d_l = 0ULL - c_al[j];
        uint64_t d_h = ~c_ah[j] + (uint64_t)(c_al[j] == 0);

        if (j == 0) {
            const uint64_t s = d_l + 127ULL;
            d_h += (uint64_t)(s < d_l);
            d_l = s;
        }
        d_ah[j] = d_h;
        d_al[j] = d_l;
    }
    A2B128(sat_bh, sat_bl, d_ah, d_al, MASKSIZE);

    for (j = 0; j < MASKSIZE; j++) {
        sat_bit[j] = sat_bh[j] >> 63;
        dif_bh[j] = q_bh[j] ^ value_sign[j];
        dif_bl[j] = q_bl[j] ^ value_sign[j];
    }
    RefreshXOR_64(sat_bit, sat_bit, MASKSIZE);
    SecMux(q_bh, q_bh, dif_bh, sat_bit);
    SecMux(q_bl, q_bl, dif_bl, sat_bit);

    for (j = 0; j < MASKSIZE; j++) {
        mu_frac_q54[j] = q_bl[j] & ((1ULL << 54) - 1ULL);
        mu_floor[j] = (q_bl[j] >> 54) ^ (q_bh[j] << 10);
    }
    RefreshXOR_64(mu_frac_q54, mu_frac_q54, MASKSIZE);
    RefreshXOR_64(mu_floor, mu_floor, MASKSIZE);
}

void SecCondAddSub(maskedb_t out, maskedb_t a, maskedb_t v, maskedb_t M) {
    maskedb_t x, sum;
    size_t i;

    for (i = 0; i < MASKSIZE; i++)
        x[i] = a[i] ^ M[i];

    SecAdd(sum, x, v, MASKSIZE);

    for (i = 0; i < MASKSIZE; i++)
        out[i] = sum[i] ^ M[i];
}

// =============================================================================
// CORDIC & Gaussian Sampler Gadgets
// =============================================================================

// Independent uniform shares; the unshared value is never formed.
void SampleUniformB(maskedb_t out) {
    size_t j;
    for (j = 0; j < MASKSIZE; j++) out[j] = rand64();
}

// Masked Falcon BaseSampler: z0 = #{i : u < RCDT[i]} for a uniform 72-bit u,
// z = beta ? z0 + 1 : -z0, corr = C(z0) = z0^2 * 0x09A7C5E0C1DDE105 * 2^47 mod 2^128.
//
// The 18 comparisons run bitsliced (lane i = threshold i) with carry-select:
// u is split into two 36-bit halves and three borrow chains run in lockstep in
// one 64-bit word, so the 72-step ripple becomes 36 steps.
//   lanes  0..17 : u[35:0]  - RCDT[i][35:0],  borrow-in 0   (L)
//   lanes 18..35 : u[71:36] - RCDT[i][71:36], borrow-in 0   (H0)
//   lanes 36..53 : u[71:36] - RCDT[i][71:36], borrow-in 1   (H1)
// Then [u < RCDT[i]] = L_i ? H1_i : H0_i.
//
// Cost: 37 SecAnd, 4 RefreshXOR_64, 1 B2A128.
#define BS_LANES     ((1ULL << 18) - 1ULL)
#define BS_LANES_HI  ((BS_LANES << 18) | (BS_LANES << 36))
#define BS_BORROW_IN (BS_LANES << 36)
#define BS_HI_COPY   ((1ULL << 18) | (1ULL << 36))
#define BS_HALF_MASK ((1ULL << 36) - 1ULL)

// Step k of all three chains: bit k of u_lo into L, bit k of u_hi into H0 and H1.
static uint64_t StepPlane(unsigned k) {
    return BASE_RCDT_PLANES[k] | BASE_RCDT_PLANES[36 + k] * BS_HI_COPY;
}

static void SpreadBits(maskedb_t ub, const uint64_t *u_lo, const uint64_t *u_hi, unsigned k) {
    size_t j;
    for (j = 0; j < MASKSIZE; j++)
        ub[j] = ((0ULL - ((u_lo[j] >> k) & 1ULL)) & BS_LANES)
              ^ ((0ULL - ((u_hi[j] >> k) & 1ULL)) & BS_LANES_HI);
}

// u_lo = u[35:0], u_hi = u[71:36], beta in bit 0; all Boolean-shared.
void SecBaseSamplerFromUniform(maskedb_t z, maskeda_t corr_h, maskeda_t corr_l,
                               maskedb_t u_hi, maskedb_t u_lo, maskedb_t beta) {
    maskedb_t b, ub, p, t, h, zneg, zdif, cbh, cbl;
    uint64_t plane;
    size_t j;
    unsigned k;

    // Borrow of u - t at one bit, t public:
    //   b' = (~u & t) ^ (~(u ^ t) & b) = (u & t) ^ t ^ ((u ^ ~t) & b)
    // Only the last product is nonlinear. Step 0 has a public borrow-in, so
    //   b' = (u & (t ^ BORROW_IN)) ^ (t | BORROW_IN)
    // is affine. Steps 1..35 chain one SecAnd each. Every u bit enters exactly
    // one step and every borrow word feeds exactly one SecAnd, so the chain is
    // a tree of NI steps and needs no refresh inside it.
    plane = StepPlane(0);
    SpreadBits(ub, u_lo, u_hi, 0);
    for (j = 0; j < MASKSIZE; j++) b[j] = ub[j] & (plane ^ BS_BORROW_IN);
    b[0] ^= plane | BS_BORROW_IN;

    for (k = 1; k < 36; k++) {
        plane = StepPlane(k);
        SpreadBits(ub, u_lo, u_hi, k);
        for (j = 0; j < MASKSIZE; j++) p[j] = ub[j];
        p[0] ^= BS_LANES ^ BS_LANES_HI ^ plane;
        SecAnd(t, p, b, MASKSIZE);
        for (j = 0; j < MASKSIZE; j++) b[j] = t[j] ^ (ub[j] & plane);
        b[0] ^= plane;
    }

    // Carry-select: h = H0 ^ (L & (H0 ^ H1)). Both SecAnd operands come from
    // the same sharing of b, so one of them is refreshed first.
    for (j = 0; j < MASKSIZE; j++) {
        p[j] = b[j] & BS_LANES;
        t[j] = ((b[j] >> 18) ^ (b[j] >> 36)) & BS_LANES;
    }
    RefreshXOR_64(t, t, MASKSIZE);
    SecAnd(h, p, t, MASKSIZE);
    for (j = 0; j < MASKSIZE; j++) h[j] ^= (b[j] >> 18) & BS_LANES;

    // h is a thermometer code: lane i = [u < RCDT[i]], so exactly lanes
    // 0..z0-1 are set and f(z0) = f(0) ^ XOR_{i<z0} (f(i) ^ f(i+1)) for any f.
    // Linear decodes, share by share:
    //   zneg = -z0, zdif = (z0 + 1) ^ (-z0), cb = C(z0).
    for (j = 0; j < MASKSIZE; j++) {
        zneg[j] = 0;
        zdif[j] = 0;
        cbh[j] = 0;
        cbl[j] = 0;
        for (k = 0; k < 18; k++) {
            const uint64_t s = 0ULL - ((h[j] >> k) & 1ULL);
            const uint64_t neg = (0ULL - k) ^ (0ULL - k - 1ULL);
            const uint64_t pos = (k + 1ULL) ^ (k + 2ULL);
            zneg[j] ^= s & neg;
            zdif[j] ^= s & (neg ^ pos);
            cbh[j] ^= s & BASE_CORR_DELTA[k][0];
            cbl[j] ^= s & BASE_CORR_DELTA[k][1];
        }
    }
    zdif[0] ^= 1ULL;

    // z = zneg ^ (-beta & zdif): beta = 1 gives z0 + 1, beta = 0 gives -z0.
    // -beta is linear share by share; one SecAnd replaces the masked increment.
    for (j = 0; j < MASKSIZE; j++) p[j] = 0ULL - (beta[j] & 1ULL);
    SecAnd(t, p, zdif, MASKSIZE);
    for (j = 0; j < MASKSIZE; j++) z[j] = zneg[j] ^ t[j];
    RefreshXOR_64(z, z, MASKSIZE);

    RefreshXOR_64(cbh, cbh, MASKSIZE);
    RefreshXOR_64(cbl, cbl, MASKSIZE);
    B2A128(corr_h, corr_l, cbh, cbl, MASKSIZE);
}

// 72 + 1 random bits per share from two words; u and beta are never unmasked.
void BaseSampler_(maskedb_t z, maskeda_t corr_h, maskeda_t corr_l) {
    maskedb_t u_hi, u_lo, beta;
    size_t j;

    for (j = 0; j < MASKSIZE; j++) {
        const uint64_t r0 = rand64();
        const uint64_t r1 = rand64();
        u_lo[j] = r0 & BS_HALF_MASK;
        u_hi[j] = r1 >> 28;
        beta[j] = r0 >> 63;
    }
    SecBaseSamplerFromUniform(z, corr_h, corr_l, u_hi, u_lo, beta);
}

void SecFxpInv(maskedb_t isig_q62, maskedb_t sigma_fpr) {
    size_t j;
    int i;
    maskedb_t X_m, Y_m, B_m;

    for (j = 0; j < MASKSIZE; j++) {
        X_m[j] = sigma_fpr[j] & 0x000FFFFFFFFFFFFFULL;
        Y_m[j] = 0;
        B_m[j] = 0;

        if (j == 0) {
            X_m[j] |= 0x0010000000000000ULL;
            Y_m[j] = 1ULL << 62;
        }

        X_m[j] <<= 10;
    }

    SecSub(Y_m, Y_m, X_m, MASKSIZE);
    RefreshXOR_64(Y_m, Y_m, MASKSIZE);

    for (i = 1; i <= 62; i++) {
        maskedb_t X_shift, dir_mask, digit;

        for (j = 0; j < MASKSIZE; j++) X_shift[j] = X_m[j] >> i;
        SecNeg(dir_mask, Y_m, 63);
        dir_mask[0] = ~dir_mask[0];

        for (j = 0; j < MASKSIZE; j++) {
            digit[j] = dir_mask[j] & 1ULL;
            B_m[j] = (B_m[j] << 1) ^ digit[j];
        }

        SecCondAddSub(Y_m, Y_m, X_shift, dir_mask);
        RefreshXOR_64(Y_m, Y_m, MASKSIZE);
    }

    for (j = 0; j < MASKSIZE; j++) isig_q62[j] = B_m[j] << 1;
    isig_q62[0] ^= 1ULL;
    RefreshXOR_64(isig_q62, isig_q62, MASKSIZE);
}

// Masked hyperbolic CORDIC: prob_q62 = ccs * exp(-x), schedule i = 1..60 with
// repeats at 4, 13 and 40. Z is only updated for i <= 20.
//
// For i >= 21 the angles are 2^(62-i), with one repeat at i = 40. Greedy "subtract
// iff Z >= 0" with consecutive powers of two is the non-restoring binary expansion
// of the residual, so on Z_21 in [-2^42, 2^42) the direction at angle 2^e is bit
// e+1 of Z_21 (the first one is the sign). The repeat restarts the expansion on a
// remainder whose low 23 bits are those of Z_21, which gives the directions for
// i >= 41 (i = 41 reads bit 22 negated). Table angles exceed their powers of two,
// so Z_21 can overshoot by up to 2^21.2; then bit 42 differs from the sign and all
// 20 directions for i = 21..40 equal the sign test, which D_m encodes with one
// SecAnd. The output is bit-identical to updating Z at every step (126 SecAdd)
// for every x_q62 in [-2^60, ln2 * 2^62 + 2^60]; this version uses 85 SecAdd.
#define CORDIC_DIGITS (((1ULL << 20) - 1ULL) << 22)

void SecFxpExp(maskedb_t prob_q62, maskedb_t x_q62, maskedb_t ccsK_q62) {
    size_t j;
    int i = 1, repeat = 0;
    maskedb_t W_m, Z_m, D_m, W_shift, angle_q62, dir_mask, ov_mask;

    RefreshXOR_64(W_m, ccsK_q62, MASKSIZE);
    for (j = 0; j < MASKSIZE; j++) Z_m[j] = x_q62[j];

    while (i <= 60) {
        // Z_m = Z_21 from here on. D_m = Z_21, except lanes 22..41 are forced to
        // "Z_21 >= 0" when Z_21 overshoots, i.e. when bit 42 differs from bit 63.
        if (i == 21 && repeat == 0) {
            for (j = 0; j < MASKSIZE; j++) {
                ov_mask[j] = (0ULL - (((Z_m[j] >> 42) ^ (Z_m[j] >> 63)) & 1ULL)) & CORDIC_DIGITS;
                D_m[j] = Z_m[j] ^ (0ULL - (Z_m[j] >> 63));
            }
            D_m[0] = ~D_m[0];
            RefreshXOR_64(D_m, D_m, MASKSIZE);
            SecAnd(D_m, ov_mask, D_m, MASKSIZE);
            for (j = 0; j < MASKSIZE; j++) D_m[j] ^= Z_m[j];
        }

        // dir_mask = all ones iff this step subtracts
        if (i <= 21) {
            SecNeg(dir_mask, Z_m, 63);
            dir_mask[0] = ~dir_mask[0];
        } else if (i == 41) {
            SecNeg(dir_mask, Z_m, 22);
            dir_mask[0] = ~dir_mask[0];
        } else {
            SecNeg(dir_mask, D_m, (unsigned)(63 - i - repeat));
        }

        if (i <= 20) {
            MaskB(angle_q62, CORDICAtanhQ62((unsigned)i));
            SecCondAddSub(Z_m, Z_m, angle_q62, dir_mask);
        }

        for (j = 0; j < MASKSIZE; j++) W_shift[j] = W_m[j] >> i;
        RefreshXOR_64(W_shift, W_shift, MASKSIZE);
        SecCondAddSub(W_m, W_m, W_shift, dir_mask);

        if ((i == 4 || i == 13 || i == 40) && !repeat) repeat = 1;
        else { i++; repeat = 0; }
    }

    RefreshXOR_64(prob_q62, W_m, MASKSIZE);
}

uint64_t SecFxpBerExp(maskedb_t accept, maskedb_t x_q54, maskedb_t ccsK_q62) {
    maskedb_t nonneg_mask, xpos_q56, xpos_bh, shift;
    maskedb_t sln2_q62, bit_mask, xpos_q62, xred_q62;
    maskedb_t prob_q62, threshold, not_thr, ge_one, sat_term;
    maskedb_t cmp0, cmp1, hi_mask, u_word, e0_word, e1_word;
    maskedb_t sig_bit, gate0, gate1, acc01;
    maskedb_t quo_bh, quo_bl;
    maskeda_t xpos_ah, xpos_al, quo_ah, quo_al;
    uint64_t accept_value;
    size_t j;
    int k;

    SecNeg(nonneg_mask, x_q54, 63);
    nonneg_mask[0] = ~nonneg_mask[0];

    SecAnd(xpos_q56, x_q54, nonneg_mask, MASKSIZE);
    for (j = 0; j < MASKSIZE; j++) xpos_q56[j] <<= 2;

    for (j = 0; j < MASKSIZE; j++) xpos_bh[j] = 0;
    B2A128(xpos_ah, xpos_al, xpos_bh, xpos_q56, MASKSIZE);
    for (j = 0; j < MASKSIZE; j++) {
        uint64_t p_h, p_l;
        Mult128(&p_h, &p_l, xpos_al[j], INV_LN2_Q56);
        quo_ah[j] = p_h + xpos_ah[j] * INV_LN2_Q56;
        quo_al[j] = p_l;
    }

    A2B128(quo_bh, quo_bl, quo_ah, quo_al, MASKSIZE);
    for (j = 0; j < MASKSIZE; j++) shift[j] = quo_bh[j] >> 48;

    SecNeg(bit_mask, shift, 0);
    for (j = 0; j < MASKSIZE; j++)
        sln2_q62[j] = bit_mask[j] & LN2_Q62;

    for (k = 1; k < 7; k++) {
        maskedb_t ln2_term;
        uint64_t shifted_ln2 = LN2_Q62 << k;

        SecNeg(bit_mask, shift, (unsigned)k);
        for (j = 0; j < MASKSIZE; j++)
            ln2_term[j] = bit_mask[j] & shifted_ln2;

        SecAdd(sln2_q62, sln2_q62, ln2_term, MASKSIZE);
        RefreshXOR_64(sln2_q62, sln2_q62, MASKSIZE);
    }

    for (j = 0; j < MASKSIZE; j++)
        xpos_q62[j] = xpos_q56[j] << 6;
    SecSub(xred_q62, xpos_q62, sln2_q62, MASKSIZE);

    SecFxpExp(prob_q62, xred_q62, ccsK_q62);

    for (j = 0; j < MASKSIZE; j++) threshold[j] = prob_q62[j] << 2;
    SecNeg(ge_one, prob_q62, 62);
    for (j = 0; j < MASKSIZE; j++) not_thr[j] = threshold[j];
    not_thr[0] = ~not_thr[0];
    SecAnd(sat_term, ge_one, not_thr, MASKSIZE);
    for (j = 0; j < MASKSIZE; j++) threshold[j] ^= sat_term[j];
    RefreshXOR_64(threshold, threshold, MASKSIZE);

    RefreshXOR_64(shift, shift, MASKSIZE);
    MaskB(cmp0, ~0ULL);
    SecVarShift(cmp0, shift);

    RefreshXOR_64(shift, shift, MASKSIZE);
    SecNeg(hi_mask, shift, 6);
    for (j = 0; j < MASKSIZE; j++) cmp1[j] = hi_mask[j];
    cmp1[0] = ~cmp1[0];

    // The three uniform coins are sampled share by share, never in the clear.
    SampleUniformB(u_word);
    SecLessThan(sig_bit, u_word, threshold);

    SampleUniformB(e0_word);
    SecLessThan(gate0, cmp0, e0_word);
    gate0[0] ^= 1ULL;

    SampleUniformB(e1_word);
    SecLessThan(gate1, cmp1, e1_word);
    gate1[0] ^= 1ULL;

    SecAnd(acc01, sig_bit, gate0, MASKSIZE);
    RefreshXOR_64(acc01, acc01, MASKSIZE);
    SecAnd(accept, acc01, gate1, MASKSIZE);

    RefreshXOR_64(accept, accept, MASKSIZE);

    UnmaskB(&accept_value, accept);

    return accept_value;
}

// =============================================================================
// Baseline only: masked fixed-point polynomial exponential (not used by COMFiZ)
// =============================================================================

typedef void (*B2A128Fn)(maskeda_t, maskeda_t, maskedb_t, maskedb_t, size_t);

// y <- c - t on arithmetic shares mod 2^128: negate every share, add c to share 0.
static void ConstMinus128(maskeda_t yh, maskeda_t yl, maskeda_t th, maskeda_t tl, uint64_t c) {
    size_t j;
    for (j = 0; j < MASKSIZE; j++) {
        yl[j] = 0ULL - tl[j];
        yh[j] = ~th[j] + (uint64_t)(tl[j] == 0);
    }
    yh[0] += (uint64_t)(yl[0] + c < yl[0]);
    yl[0] += c;
}

// t <- floor(p / 2^63) mod 2^64, exactly: A2B128, take bits 63..126, then back to
// arithmetic shares with the given B2A.
static void Trunc63(maskeda_t th, maskeda_t tl, maskeda_t ph, maskeda_t pl, B2A128Fn b2a) {
    maskedb_t bh, bl, zero;
    size_t j;
    A2B128(bh, bl, ph, pl, MASKSIZE);
    for (j = 0; j < MASKSIZE; j++) {
        bl[j] = (bh[j] << 1) ^ (bl[j] >> 63);
        zero[j] = 0;
    }
    b2a(th, tl, zero, bl, MASKSIZE);
}

// prob_q62 = ccs * exp(-x) in Q62, for x_q62 in [0, ln2) and ccs_q63 = ccs in Q63.
// Falcon's ApproxExp (degree-12 Horner in Q63) on arithmetic shares mod 2^128, with
// every truncation exact, so the output equals the clear ApproxExp bit for bit.
// Cost: 12 SecMult128, 13 A2B128 and 14 B2A128.
static void PolyExp(maskedb_t prob_q62, maskedb_t x_q62, maskedb_t ccs_q63, B2A128Fn b2a) {
    maskedb_t zb, zero, bh, bl;
    maskeda_t zh, zl, ch, cl, yh, yl, ph, pl, th, tl;
    size_t j;
    int u;

    for (j = 0; j < MASKSIZE; j++) {
        zb[j] = x_q62[j] << 1;
        zero[j] = 0;
    }
    b2a(zh, zl, zero, zb, MASKSIZE);
    b2a(ch, cl, zero, ccs_q63, MASKSIZE);

    // the first product is by the public C[0], so it is share-local
    for (j = 0; j < MASKSIZE; j++) {
        Mult128(&ph[j], &pl[j], zl[j], APPROXEXP_C[0]);
        ph[j] += zh[j] * APPROXEXP_C[0];
    }
    for (u = 1; u <= 12; u++) {
        if (u > 1) SecMult128(ph, pl, zh, zl, yh, yl);
        Trunc63(th, tl, ph, pl, b2a);
        ConstMinus128(yh, yl, th, tl, APPROXEXP_C[u]);
    }

    // ccs * y < 2^127, so its top word is (ccs * y) >> 64
    SecMult128(ph, pl, ch, cl, yh, yl);
    A2B128(bh, bl, ph, pl, MASKSIZE);
    RefreshXOR_64(prob_q62, bh, MASKSIZE);
}

// With the B2A of Coron (CHES 2017): fastest for few shares, exponential in d.
void SecFxpPolyExp(maskedb_t prob_q62, maskedb_t x_q62, maskedb_t ccs_q63) {
    PolyExp(prob_q62, x_q62, ccs_q63, B2A128);
}

// With the B2A of Coron, Grosschadl and Vadnala (CHES 2014): quadratic in d.
void SecFxpPolyExpQuad(maskedb_t prob_q62, maskedb_t x_q62, maskedb_t ccs_q63) {
    PolyExp(prob_q62, x_q62, ccs_q63, B2A128Quad);
}
