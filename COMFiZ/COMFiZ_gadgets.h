#ifndef COMFIZ_GADGETS_H
#define COMFIZ_GADGETS_H

#include <stddef.h>
#include <stdint.h>

#include "gadgets.h"
#include "utils.h"

#define SIGMA_MIN_OVER_K_Q62 0x62C0310A4F066350ULL

void SecNeg(maskedb_t out, maskedb_t in, unsigned pos);
void SecMux(maskedb_t out, maskedb_t x, maskedb_t y, maskedb_t cond);
void SecLessThan(maskedb_t out, maskedb_t a, maskedb_t b);
void SecVarShift(maskedb_t reg, maskedb_t shift);
void SecFprIrsh128(maskedb_t out_bh, maskedb_t out_bl, maskedb_t in_bh, maskedb_t in_bl, maskeda_t c_a);
void SecFprToInt128(maskedb_t mu_floor, maskedb_t mu_frac_q54, maskedb_t mu_fpr);
void SecCondAddSub(maskedb_t out, maskedb_t a, maskedb_t v, maskedb_t M);

void BaseSampler_(maskedb_t z, uint64_t *corr_h, uint64_t *corr_l);
void SecFxpInv(maskedb_t isig_q62, maskedb_t sigma_fpr);
void SecFxpExp(maskedb_t prob_q62, maskedb_t x_q62, maskedb_t ccsK_q62);
uint64_t SecFxpBerExp(maskedb_t accept, maskedb_t x_q54, maskedb_t ccsK_q62);

#endif
