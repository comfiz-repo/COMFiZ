#include <stddef.h>
#include <stdint.h>

#include "COMFiZ.h"
#include "COMFiZ_gadgets.h"
#include "gadgets.h"
#include "utils.h"

void COMFiZ(maskedb_t sample, maskedb_t mu_fpr, maskedb_t sigma_fpr) {
    maskedb_t accept, mu_floor, mu_frac_q54, isig_q62, isig_bh, ccsK_q62;
    maskedb_t ccs_bh, ccs_bl;
    maskeda_t isig_ah, isig_al, ccs_ah, ccs_al;
    size_t i;

    SecFprToInt128(mu_floor, mu_frac_q54, mu_fpr);

    // 1. isig = 1 / sigma_fpr, Q62
    SecFxpInv(isig_q62, sigma_fpr);

    for (i = 0; i < MASKSIZE; i++) isig_bh[i] = 0;
    B2A128(isig_ah, isig_al, isig_bh, isig_q62, MASKSIZE);

    // 2. ccsK = (sigma_min / K) * isig
    for (i = 0; i < MASKSIZE; i++) {
        uint64_t p_h, p_l;

        Mult128(&p_h, &p_l, isig_al[i], SIGMA_MIN_OVER_K_Q62);
        ccs_ah[i] = p_h + isig_ah[i] * SIGMA_MIN_OVER_K_Q62;
        ccs_al[i] = p_l;
    }
    A2B128(ccs_bh, ccs_bl, ccs_ah, ccs_al, MASKSIZE);
    for (i = 0; i < MASKSIZE; i++)
        ccsK_q62[i] = (ccs_bh[i] << 2) ^ (ccs_bl[i] >> 62);

    while (1) {
        maskedb_t z, z_q54, delta_q54, sdel_q54, x_q54, sgn_bh;
        maskedb_t prod_bh, prod_bl;
        maskeda_t op_ah, op_al, prod_ah, prod_al;
        uint64_t corr_h, corr_l, borrow;

        BaseSampler_(z, &corr_h, &corr_l);

        // delta = z - mu_frac in Q54
        for (i = 0; i < MASKSIZE; i++) z_q54[i] = z[i] << 54;
        SecSub(delta_q54, z_q54, mu_frac_q54, MASKSIZE);

        SecNeg(sgn_bh, delta_q54, 63);

        B2A128(op_ah, op_al, sgn_bh, delta_q54, MASKSIZE);
        SecMult128(prod_ah, prod_al, op_ah, op_al, isig_ah, isig_al);
        A2B128(prod_bh, prod_bl, prod_ah, prod_al, MASKSIZE);

        for (i = 0; i < MASKSIZE; i++)
            sdel_q54[i] = (prod_bh[i] << 2) ^ (prod_bl[i] >> 62);

        SecNeg(sgn_bh, sdel_q54, 63);
        B2A128(op_ah, op_al, sgn_bh, sdel_q54, MASKSIZE);
        SecSqr128(prod_ah, prod_al, op_ah, op_al);

        borrow = (uint64_t)(prod_al[0] < corr_l);
        prod_ah[0] = prod_ah[0] - corr_h - borrow;
        prod_al[0] = prod_al[0] - corr_l;

        A2B128(prod_bh, prod_bl, prod_ah, prod_al, MASKSIZE);

        for (i = 0; i < MASKSIZE; i++)
            x_q54[i] = (prod_bh[i] << 9) ^ (prod_bl[i] >> 55);

        if (SecFxpBerExp(accept, x_q54, ccsK_q62)) {
            SecAdd(sample, z, mu_floor, MASKSIZE);
            RefreshXOR_64(sample, sample, MASKSIZE);
            return;
        }
    }
}
