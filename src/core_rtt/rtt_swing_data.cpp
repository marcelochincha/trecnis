/*
 * rtt_swing_data.cpp -- one character's swing solvers and ballfudge rows as generated literals
 * (tools/swing_data/gen_swing_sets.py -> data/swing_sets_<char>.inc). No parsing at runtime.
 */
#include "rtt_swing.h"

#include "data/swing_sets_sweden.inc"
#include "data/swing_sets_usa.inc"
#include "data/swing_sets_austria.inc"

#define N_OF(a) (int)(sizeof(a) / sizeof(a[0]))
#define SWING_DATA(fn, p)                                                                   \
    void fn(CharacterSwingData* d) {                                                        \
        d->sets[SOLVER_TS] = k_##p##_ts;         d->count[SOLVER_TS] = N_OF(k_##p##_ts);         \
        d->sets[SOLVER_BS] = k_##p##_bs;         d->count[SOLVER_BS] = N_OF(k_##p##_bs);         \
        d->sets[SOLVER_TS_ITZ] = k_##p##_ts_itz; d->count[SOLVER_TS_ITZ] = N_OF(k_##p##_ts_itz); \
        d->sets[SOLVER_BS_ITZ] = k_##p##_bs_itz; d->count[SOLVER_BS_ITZ] = N_OF(k_##p##_bs_itz); \
        d->sets[SOLVER_SFT] = k_##p##_sft;       d->count[SOLVER_SFT] = N_OF(k_##p##_sft);       \
        d->fudge = k_##p##_fudge;                                                           \
        d->anims = k_##p##_anims;                                                           \
        d->anim_count = N_OF(k_##p##_anims);                                                \
    }

SWING_DATA(swing_data_sweden, swe)
SWING_DATA(swing_data_usa, usa)          /* Haley's style (capture 11) */
SWING_DATA(swing_data_austria, aus)      /* Jurgen's style (capture 11) */
