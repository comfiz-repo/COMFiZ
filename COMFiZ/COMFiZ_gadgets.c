#include <stddef.h>
#include <stdint.h>

#include "COMFiZ_gadgets.h"
#include "gadgets.h"
#include "utils.h"

// =============================================================================
// Constants & Lookup Tables
// =============================================================================

static const uint64_t INV_2_SIGMA_MAX_SQ_Q62 = 0x09A7C5E0C1DDE105ULL;
static const uint64_t LN2_Q62                 = 0x2C5C85FDF473DE6BULL;
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

static const uint64_t BASE_RCDT[18][2] = {
    {0xA3FULL, 0x7F42ED3AC391802ULL},
    {0x54DULL, 0x32B181F3F7DDB82ULL},
    {0x227ULL, 0xDCDD0934829C1FFULL},
    {0x0ADULL, 0x1754377C7994AE4ULL},
    {0x029ULL, 0x5846CAEF33F1F6FULL},
    {0x007ULL, 0x74AC754ED74BD5FULL},
    {0x001ULL, 0x024DD542B776AE4ULL},
    {0x000ULL, 117656387352093658ULL},
    {0x000ULL, 8867391802663976ULL},
    {0x000ULL, 496969357462633ULL},
    {0x000ULL, 20680885154299ULL},
    {0x000ULL, 638331848991ULL},
    {0x000ULL, 14602316184ULL},
    {0x000ULL, 247426747ULL},
    {0x000ULL, 3104126ULL},
    {0x000ULL, 28824ULL},
    {0x000ULL, 198ULL},
    {0x000ULL, 1ULL}
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

void BaseSampler_(maskedb_t z, uint64_t *corr_h, uint64_t *corr_l) {
    const uint64_t r0 = rand64();
    const uint64_t r1 = rand64();
    const uint64_t u1 = r0 & 0x0FFFFFFFFFFFFFFFULL;
    const uint64_t u0 = (r0 >> 60) | ((r1 & 0xFFULL) << 4);
    const uint64_t b = (r1 >> 8) & 1ULL;
    uint64_t bmask, z0 = 0;
    uint64_t p_h, p_l;
    size_t i;

    for (i = 0; i < 18; i++) {
        const uint64_t hi_lt = (uint64_t)(u0 < BASE_RCDT[i][0]);
        const uint64_t hi_eq = (uint64_t)(u0 == BASE_RCDT[i][0]);
        const uint64_t lo_lt = (uint64_t)(u1 < BASE_RCDT[i][1]);

        z0 += hi_lt | (hi_eq & lo_lt);
    }

    bmask = 0ULL - b;
    MaskB(z, ((z0 + 1ULL) & bmask) ^ ((0ULL - z0) & ~bmask));

    Mult128(&p_h, &p_l, z0 * z0, INV_2_SIGMA_MAX_SQ_Q62);
    *corr_h = (p_h << 47) ^ (p_l >> 17);
    *corr_l = p_l << 47;
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

void SecFxpExp(maskedb_t prob_q62, maskedb_t x_q62, maskedb_t ccsK_q62) {
    size_t j;
    int i = 1, repeat = 0;
    maskedb_t W_m, Z_m;

    RefreshXOR_64(W_m, ccsK_q62, MASKSIZE);
    for (j = 0; j < MASKSIZE; j++) Z_m[j] = x_q62[j];

    while (i <= 60) {
        maskedb_t W_shift, angle_q62, dir_mask;

        for (j = 0; j < MASKSIZE; j++) W_shift[j] = W_m[j] >> i;
        RefreshXOR_64(W_shift, W_shift, MASKSIZE);

        SecNeg(dir_mask, Z_m, 63);
        dir_mask[0] = ~dir_mask[0];

        MaskB(angle_q62, CORDICAtanhQ62((unsigned)i));

        SecCondAddSub(Z_m, Z_m, angle_q62, dir_mask);
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

    MaskB(u_word, rand64());
    SecLessThan(sig_bit, u_word, threshold);

    MaskB(e0_word, rand64());
    SecLessThan(gate0, cmp0, e0_word);
    gate0[0] ^= 1ULL;

    MaskB(e1_word, rand64());
    SecLessThan(gate1, cmp1, e1_word);
    gate1[0] ^= 1ULL;

    SecAnd(acc01, sig_bit, gate0, MASKSIZE);
    RefreshXOR_64(acc01, acc01, MASKSIZE);
    SecAnd(accept, acc01, gate1, MASKSIZE);

    RefreshXOR_64(accept, accept, MASKSIZE);

    UnmaskB(&accept_value, accept);

    return accept_value;
}
