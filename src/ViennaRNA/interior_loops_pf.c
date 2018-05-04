#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <string.h>
#include "ViennaRNA/fold_vars.h"
#include "ViennaRNA/alphabet.h"
#include "ViennaRNA/utils.h"
#include "ViennaRNA/constraints.h"
#include "ViennaRNA/exterior_loops.h"
#include "ViennaRNA/gquad.h"
#include "ViennaRNA/structured_domains.h"
#include "ViennaRNA/unstructured_domains.h"
#include "ViennaRNA/interior_loops.h"


#ifdef __GNUC__
# define INLINE inline
#else
# define INLINE
#endif

#include "interior_loops_hc.inc"
#include "interior_loops_sc_pf.inc"

/*
 #################################
 # PRIVATE FUNCTION DECLARATIONS #
 #################################
 */

PRIVATE FLT_OR_DBL
exp_E_int_loop(vrna_fold_compound_t *vc,
               int                  i,
               int                  j);


PRIVATE FLT_OR_DBL
exp_E_ext_int_loop(vrna_fold_compound_t *vc,
                   int                  p,
                   int                  q);


PRIVATE FLT_OR_DBL
exp_E_interior_loop(vrna_fold_compound_t  *vc,
                    int                   i,
                    int                   j,
                    int                   k,
                    int                   l);


/*
 #################################
 # BEGIN OF FUNCTION DEFINITIONS #
 #################################
 */
PUBLIC FLT_OR_DBL
vrna_exp_E_int_loop(vrna_fold_compound_t  *vc,
                    int                   i,
                    int                   j)
{
  FLT_OR_DBL q = 0.;

  if ((vc) && (i > 0) && (j > 0)) {
    if (j < i) {
      /* Note: j < i indicates that we want to evaluate exterior int loop (for circular RNAs)! */
      if (vc->hc->type == VRNA_HC_WINDOW) {
        vrna_message_warning(
          "vrna_exp_E_int_loop: invalid sequence positions for pair (i,j) = (%d,%d)!",
          i,
          j);
      } else {
        q = exp_E_ext_int_loop(vc, j, i);
      }
    } else {
      q = exp_E_int_loop(vc, i, j);
    }
  }

  return q;
}


PUBLIC FLT_OR_DBL
vrna_exp_E_interior_loop(vrna_fold_compound_t *vc,
                         int                  i,
                         int                  j,
                         int                  k,
                         int                  l)
{
  if (vc)
    return exp_E_interior_loop(vc, i, j, k, l);

  return 0.;
}


PRIVATE FLT_OR_DBL
exp_E_int_loop(vrna_fold_compound_t *fc,
               int                  i,
               int                  j)
{
  unsigned char             sliding_window, hc_decompose_ij, hc_decompose_kl;
  char                      *ptype, **ptype_local;
  unsigned char             *hc_mx, **hc_mx_local;
  short                     *S1, **SS, **S5, **S3;
  unsigned int              *sn, *se, *ss, n_seq, s, **a2s;
  int                       *rtype, noclose, *my_iindx, *jindx, *hc_up, ij,
                            with_gquad, with_ud;
  FLT_OR_DBL                qbt1, q_temp, *qb, **qb_local, *G, *scale;
  vrna_exp_param_t          *pf_params;
  vrna_md_t                 *md;
  vrna_ud_t                 *domains_up;
  eval_hc                   *evaluate;
  struct  default_data      hc_dat_local;
  struct sc_wrapper_exp_int sc_wrapper;

  sliding_window  = (fc->hc->type == VRNA_HC_WINDOW) ? 1 : 0;
  n_seq           = (fc->type == VRNA_FC_TYPE_SINGLE) ? 1 : fc->n_seq;
  sn              = fc->strand_number;
  se              = fc->strand_end;
  ss              = fc->strand_start;
  ptype           = (fc->type == VRNA_FC_TYPE_SINGLE) ? (sliding_window ? NULL : fc->ptype) : NULL;
  ptype_local     =
    (fc->type == VRNA_FC_TYPE_SINGLE) ? (sliding_window ? fc->ptype_local : NULL) : NULL;
  S1          = (fc->type == VRNA_FC_TYPE_SINGLE) ? fc->sequence_encoding : NULL;
  SS          = (fc->type == VRNA_FC_TYPE_SINGLE) ? NULL : fc->S;
  S5          = (fc->type == VRNA_FC_TYPE_SINGLE) ? NULL : fc->S5;
  S3          = (fc->type == VRNA_FC_TYPE_SINGLE) ? NULL : fc->S3;
  a2s         = (fc->type == VRNA_FC_TYPE_SINGLE) ? NULL : fc->a2s;
  qb          = (sliding_window) ? NULL : fc->exp_matrices->qb;
  G           = (sliding_window) ? NULL : fc->exp_matrices->G;
  qb_local    = (sliding_window) ? fc->exp_matrices->qb_local : NULL;
  scale       = fc->exp_matrices->scale;
  my_iindx    = fc->iindx;
  jindx       = fc->jindx;
  hc_mx       = (sliding_window) ? NULL : fc->hc->matrix;
  hc_mx_local = (sliding_window) ? fc->hc->matrix_local : NULL;
  hc_up       = fc->hc->up_int;
  pf_params   = fc->exp_params;
  md          = &(pf_params->model_details);
  with_gquad  = md->gquad;
  domains_up  = fc->domains_up;
  with_ud     = ((domains_up) && (domains_up->exp_energy_cb)) ? 1 : 0;
  rtype       = &(md->rtype[0]);
  qbt1        = 0.;
  evaluate    = prepare_hc_default(fc, &hc_dat_local);

  init_sc_wrapper(fc, &sc_wrapper);

  ij = (sliding_window) ? 0 : jindx[j] + i;

  hc_decompose_ij = (sliding_window) ? hc_mx_local[i][j - i] : hc_mx[ij];

  /* CONSTRAINED INTERIOR LOOP start */
  if (hc_decompose_ij & VRNA_CONSTRAINT_CONTEXT_INT_LOOP) {
    unsigned int  type, type2, *tt;
    int           k, l, kl, last_k, first_l, u1, u2, turn, noGUclosure;

    turn        = md->min_loop_size;
    noGUclosure = md->noGUclosure;
    tt          = NULL;
    type        = 0;

    if (fc->type == VRNA_FC_TYPE_SINGLE)
      type = sliding_window ?
             vrna_get_ptype_window(i, j + i, ptype_local) :
             vrna_get_ptype(ij, ptype);

    noclose = ((noGUclosure) && (type == 3 || type == 4)) ? 1 : 0;

    if (fc->type == VRNA_FC_TYPE_COMPARATIVE) {
      tt = (unsigned int *)vrna_alloc(sizeof(unsigned int) * n_seq);
      for (s = 0; s < n_seq; s++)
        tt[s] = vrna_get_ptype_md(SS[s][i], SS[s][j], md);
    }

    /* handle stacks separately */
    k = i + 1;
    l = j - 1;
    if ((k < l) && (sn[i] == sn[k]) && (sn[l] == sn[j])) {
      kl              = (sliding_window) ? 0 : jindx[l] + k;
      hc_decompose_kl = (sliding_window) ? hc_mx_local[k][l - k] : hc_mx[kl];
      if ((hc_decompose_kl & VRNA_CONSTRAINT_CONTEXT_INT_LOOP_ENC) &&
          (evaluate(i, j, k, l, &hc_dat_local))) {
        q_temp = (sliding_window) ? qb_local[k][l] : qb[my_iindx[k] - l];

        switch (fc->type) {
          case VRNA_FC_TYPE_SINGLE:
            type2 = sliding_window ?
                    rtype[vrna_get_ptype_window(k, l + k, ptype_local)] :
                    rtype[vrna_get_ptype(kl, ptype)];

            q_temp *= exp_E_IntLoop(0,
                                    0,
                                    type,
                                    type2,
                                    S1[i + 1],
                                    S1[j - 1],
                                    S1[k - 1],
                                    S1[l + 1],
                                    pf_params);

            break;

          case VRNA_FC_TYPE_COMPARATIVE:
            for (s = 0; s < n_seq; s++) {
              type2   = vrna_get_ptype_md(SS[s][l], SS[s][k], md);
              q_temp  *= exp_E_IntLoop(0,
                                       0,
                                       tt[s],
                                       type2,
                                       S3[s][i],
                                       S5[s][j],
                                       S5[s][k],
                                       S3[s][l],
                                       pf_params);
            }
            break;
        }

        if (sc_wrapper.pair)
          q_temp *= sc_wrapper.pair(i, j, k, l, &sc_wrapper);

        qbt1 += q_temp *
                scale[2];
      }
    }

    if (!noclose) {
      /* only proceed if the enclosing pair is allowed */

      /* handle bulges in 5' side */
      l = j - 1;
      if ((l > i + 2) && (sn[j] == sn[l])) {
        last_k = l - turn - 1;

        if (last_k > i + 1 + MAXLOOP)
          last_k = i + 1 + MAXLOOP;

        if (last_k > i + 1 + hc_up[i + 1])
          last_k = i + 1 + hc_up[i + 1];

        if (last_k > se[sn[i]])
          last_k = se[sn[i]];

        u1 = 1;

        k   = i + 2;
        kl  = (sliding_window) ? 0 : jindx[l] + k;
        for (; k <= last_k; k++, u1++, kl++) {
          hc_decompose_kl = (sliding_window) ? hc_mx_local[k][l - k] : hc_mx[kl];
          if ((hc_decompose_kl & VRNA_CONSTRAINT_CONTEXT_INT_LOOP_ENC) &&
              (evaluate(i, j, k, l, &hc_dat_local))) {
            q_temp = (sliding_window) ? qb_local[k][l] : qb[my_iindx[k] - l];

            switch (fc->type) {
              case VRNA_FC_TYPE_SINGLE:
                type2 = sliding_window ?
                        rtype[vrna_get_ptype_window(k, l + k, ptype_local)] :
                        rtype[vrna_get_ptype(kl, ptype)];

                if ((noGUclosure) && (type2 == 3 || type2 == 4))
                  continue;

                q_temp *= exp_E_IntLoop(u1,
                                        0,
                                        type,
                                        type2,
                                        S1[i + 1],
                                        S1[j - 1],
                                        S1[k - 1],
                                        S1[l + 1],
                                        pf_params);

                break;

              case VRNA_FC_TYPE_COMPARATIVE:
                for (s = 0; s < n_seq; s++) {
                  int u1_local = a2s[s][k - 1] - a2s[s][i];
                  type2   = vrna_get_ptype_md(SS[s][l], SS[s][k], md);
                  q_temp  *= exp_E_IntLoop(u1_local,
                                           0,
                                           tt[s],
                                           type2,
                                           S3[s][i],
                                           S5[s][j],
                                           S5[s][k],
                                           S3[s][l],
                                           pf_params);
                }
                break;
            }

            if (sc_wrapper.pair)
              q_temp *= sc_wrapper.pair(i, j, k, l, &sc_wrapper);

            qbt1 += q_temp *
                    scale[u1 + 2];

            if (with_ud) {
              q_temp *= domains_up->exp_energy_cb(fc,
                                                  i + 1, k - 1,
                                                  VRNA_UNSTRUCTURED_DOMAIN_INT_LOOP,
                                                  domains_up->data);
              qbt1 += q_temp *
                      scale[u1 + 2];
            }
          }
        }
      }

      /* handle bulges in 3' side */
      k = i + 1;
      if ((k < j - 2) && (sn[i] == sn[k])) {
        first_l = k + turn + 1;
        if (first_l < j - 1 - MAXLOOP)
          first_l = j - 1 - MAXLOOP;

        if (first_l < ss[sn[j]])
          first_l = ss[sn[j]];

        u2 = 1;
        for (l = j - 2; l >= first_l; l--, u2++) {
          if (u2 > hc_up[l + 1])
            break;

          kl = (sliding_window) ? 0 : jindx[l] + k;

          hc_decompose_kl = (sliding_window) ? hc_mx_local[k][l - k] : hc_mx[kl];
          if ((hc_decompose_kl & VRNA_CONSTRAINT_CONTEXT_INT_LOOP_ENC) &&
              (evaluate(i, j, k, l, &hc_dat_local))) {
            q_temp = (sliding_window) ? qb_local[k][l] : qb[my_iindx[k] - l];

            switch (fc->type) {
              case VRNA_FC_TYPE_SINGLE:
                type2 = sliding_window ?
                        rtype[vrna_get_ptype_window(k, l + k, ptype_local)] :
                        rtype[vrna_get_ptype(kl, ptype)];

                if ((noGUclosure) && (type2 == 3 || type2 == 4))
                  continue;

                q_temp *= exp_E_IntLoop(0,
                                        u2,
                                        type,
                                        type2,
                                        S1[i + 1],
                                        S1[j - 1],
                                        S1[k - 1],
                                        S1[l + 1],
                                        pf_params);

                break;

              case VRNA_FC_TYPE_COMPARATIVE:
                for (s = 0; s < n_seq; s++) {
                  int u2_local = a2s[s][j - 1] - a2s[s][l];
                  type2   = vrna_get_ptype_md(SS[s][l], SS[s][k], md);
                  q_temp  *= exp_E_IntLoop(0,
                                           u2_local,
                                           tt[s],
                                           type2,
                                           S3[s][i],
                                           S5[s][j],
                                           S5[s][k],
                                           S3[s][l],
                                           pf_params);
                }
                break;
            }

            if (sc_wrapper.pair)
              q_temp *= sc_wrapper.pair(i, j, k, l, &sc_wrapper);

            qbt1 += q_temp *
                    scale[u2 + 2];

            if (with_ud) {
              q_temp *= domains_up->exp_energy_cb(fc,
                                                  l + 1, j - 1,
                                                  VRNA_UNSTRUCTURED_DOMAIN_INT_LOOP,
                                                  domains_up->data);
              qbt1 += q_temp *
                      scale[u2 + 2];
            }
          }
        }
      }

      /* last but not least, all other internal loops */
      last_k = j - turn - 3;

      if (last_k > i + MAXLOOP + 1)
        last_k = i + MAXLOOP + 1;

      if (last_k > i + 1 + hc_up[i + 1])
        last_k = i + 1 + hc_up[i + 1];

      if (last_k > se[sn[i]])
        last_k = se[sn[i]];

      u1 = 1;

      for (k = i + 2; k <= last_k; k++, u1++) {
        first_l = k + turn + 1;

        if (first_l < j - 1 - MAXLOOP + u1)
          first_l = j - 1 - MAXLOOP + u1;

        if (first_l < ss[sn[j]])
          first_l = ss[sn[j]];

        u2 = 1;

        for (l = j - 2; l >= first_l; l--, u2++) {
          if (hc_up[l + 1] < u2)
            break;

          kl              = (sliding_window) ? 0 : jindx[l] + k;
          hc_decompose_kl = (sliding_window) ? hc_mx_local[k][l - k] : hc_mx[kl];
          if ((hc_decompose_kl & VRNA_CONSTRAINT_CONTEXT_INT_LOOP_ENC) &&
              (evaluate(i, j, k, l, &hc_dat_local))) {
            q_temp = (sliding_window) ? qb_local[k][l] : qb[my_iindx[k] - l];

            switch (fc->type) {
              case VRNA_FC_TYPE_SINGLE:
                type2 = sliding_window ?
                        rtype[vrna_get_ptype_window(k, l + k, ptype_local)] :
                        rtype[vrna_get_ptype(kl, ptype)];

                if ((noGUclosure) && (type2 == 3 || type2 == 4))
                  continue;

                q_temp *= exp_E_IntLoop(u1,
                                        u2,
                                        type,
                                        type2,
                                        S1[i + 1],
                                        S1[j - 1],
                                        S1[k - 1],
                                        S1[l + 1],
                                        pf_params);

                break;

              case VRNA_FC_TYPE_COMPARATIVE:
                for (s = 0; s < n_seq; s++) {
                  int u1_local  = a2s[s][k - 1] - a2s[s][i];
                  int u2_local  = a2s[s][j - 1] - a2s[s][l];
                  type2   = vrna_get_ptype_md(SS[s][l], SS[s][k], md);
                  q_temp  *= exp_E_IntLoop(u1_local,
                                           u2_local,
                                           tt[s],
                                           type2,
                                           S3[s][i],
                                           S5[s][j],
                                           S5[s][k],
                                           S3[s][l],
                                           pf_params);
                }

                break;
            }

            if (sc_wrapper.pair)
              q_temp *= sc_wrapper.pair(i, j, k, l, &sc_wrapper);

            qbt1 += q_temp *
                    scale[u1 + u2 + 2];

            if (with_ud) {
              FLT_OR_DBL q5, q3;

              q5 = domains_up->exp_energy_cb(fc,
                                             i + 1, k - 1,
                                             VRNA_UNSTRUCTURED_DOMAIN_INT_LOOP,
                                             domains_up->data);
              q3 = domains_up->exp_energy_cb(fc,
                                             l + 1, j - 1,
                                             VRNA_UNSTRUCTURED_DOMAIN_INT_LOOP,
                                             domains_up->data);

              qbt1 += q_temp *
                      q5 *
                      scale[u1 + u2 + 2];
              qbt1 += q_temp *
                      q3 *
                      scale[u1 + u2 + 2];
              qbt1 += q_temp *
                      q5 *
                      q3 *
                      scale[u1 + u2 + 2];
            }
          }
        }
      }

      if (with_gquad) {
        switch (fc->type) {
          case VRNA_FC_TYPE_SINGLE:
            if (!noclose) {
              if (sliding_window) {
                /* no G-Quadruplex support for sliding window partition function yet! */
              } else if (sn[j] == sn[i]) {
                qbt1 += exp_E_GQuad_IntLoop(i, j, type, S1, G, scale, my_iindx, pf_params);
              }
            }

            break;

          case VRNA_FC_TYPE_COMPARATIVE:
            /* no G-Quadruplex support for comparative partition function yet! */
            break;
        }
      }
    }

    free(tt);
  }

  free_sc_wrapper(&sc_wrapper);

  return qbt1;
}


PRIVATE FLT_OR_DBL
exp_E_ext_int_loop(vrna_fold_compound_t *fc,
                   int                  i,
                   int                  j)
{
  unsigned char             *hc_mx, eval_loop;
  short                     *S, *S2, **SS, **S5, **S3;
  unsigned int              *tt, n_seq, s, **a2s, type, type2;
  int                       ij, kl, k, l, u1, u2, u3, qmin, with_ud,
                            length, *my_iindx, *indx, *hc_up, turn;
  FLT_OR_DBL                q, q_temp, *qb, *scale;
  vrna_exp_param_t          *pf_params;
  vrna_md_t                 *md;
  vrna_ud_t                 *domains_up;
  eval_hc                   *evaluate;
  struct default_data       hc_dat_local;
  struct sc_wrapper_exp_int sc_wrapper;

  n_seq       = (fc->type == VRNA_FC_TYPE_SINGLE) ? 1 : fc->n_seq;
  S           = (fc->type == VRNA_FC_TYPE_SINGLE) ? fc->sequence_encoding : NULL;
  S2          = (fc->type == VRNA_FC_TYPE_SINGLE) ? fc->sequence_encoding2 : NULL;
  SS          = (fc->type == VRNA_FC_TYPE_SINGLE) ? NULL : fc->S;
  S5          = (fc->type == VRNA_FC_TYPE_SINGLE) ? NULL : fc->S5;
  S3          = (fc->type == VRNA_FC_TYPE_SINGLE) ? NULL : fc->S3;
  a2s         = (fc->type == VRNA_FC_TYPE_SINGLE) ? NULL : fc->a2s;
  length      = fc->length;
  my_iindx    = fc->iindx;
  indx        = fc->jindx;
  qb          = fc->exp_matrices->qb;
  scale       = fc->exp_matrices->scale;
  hc_mx       = fc->hc->matrix;
  hc_up       = fc->hc->up_int;
  pf_params   = fc->exp_params;
  md          = &(pf_params->model_details);
  turn        = md->min_loop_size;
  type        = 0;
  tt          = NULL;
  ij          = indx[j] + i;
  domains_up  = fc->domains_up;
  with_ud     = ((domains_up) && (domains_up->exp_energy_cb)) ? 1 : 0;

  q = 0.;

  evaluate = prepare_hc_default(fc, &hc_dat_local);

  init_sc_wrapper(fc, &sc_wrapper);

  /* CONSTRAINED INTERIOR LOOP start */
  if (hc_mx[ij] & VRNA_CONSTRAINT_CONTEXT_INT_LOOP) {
    /* prepare necessary variables */
    if (fc->type == VRNA_FC_TYPE_SINGLE) {
      type = vrna_get_ptype_md(S2[j], S2[i], md);
    } else {
      tt = (unsigned int *)vrna_alloc(sizeof(unsigned int) * n_seq);

      for (s = 0; s < n_seq; s++)
        tt[s] = vrna_get_ptype_md(SS[s][j], SS[s][i], md);
    }

    for (k = j + 1; k < length; k++) {
      u2 = k - j - 1;
      if (u2 + i - 1 > MAXLOOP)
        break;

      if (hc_up[j + 1] < u2)
        break;

      qmin = u2 + i - 1 + length - MAXLOOP;
      if (qmin < k + turn + 1)
        qmin = k + turn + 1;

      for (l = length; l >= qmin; l--) {
        u1  = i - 1;
        u3  = length - l;
        if (hc_up[l + 1] < (u1 + u3))
          break;

        if (u1 + u2 + u3 > MAXLOOP)
          continue;

        kl = indx[l] + k;

        eval_loop = hc_mx[kl] & VRNA_CONSTRAINT_CONTEXT_INT_LOOP;

        if (eval_loop && evaluate(i, j, k, l, &hc_dat_local)) {
          q_temp = qb[my_iindx[k] - l];

          switch (fc->type) {
            case VRNA_FC_TYPE_SINGLE:
              type2 = vrna_get_ptype_md(S2[l], S2[k], md);

              /* regular interior loop */
              q_temp *=
                exp_E_IntLoop(u2,
                              u1 + u3,
                              type,
                              type2,
                              S[j + 1],
                              S[i - 1],
                              S[k - 1],
                              S[l + 1],
                              pf_params);
              break;

            case VRNA_FC_TYPE_COMPARATIVE:
              for (s = 0; s < n_seq; s++) {
                type2 = vrna_get_ptype_md(SS[s][l], SS[s][k], md);
                u1    = a2s[s][i - 1];
                u2    = a2s[s][k - 1] - a2s[s][j];
                u3    = a2s[s][length] - a2s[s][l];
                l     *= exp_E_IntLoop(u2,
                                       u1 + u3,
                                       tt[s],
                                       type2,
                                       S3[s][j],
                                       S5[s][i],
                                       S5[s][k],
                                       S3[s][l],
                                       pf_params);
              }
              break;
          }

          if (sc_wrapper.pair_ext)
            q_temp *= sc_wrapper.pair_ext(i, j, k, l, &sc_wrapper);

          q += q_temp *
               scale[u1 + u2 + u3];

          if (with_ud) {
            FLT_OR_DBL q5, q3;

            q5  = q3 = 0.;
            u1  = i - 1;
            u2  = k - j - 1;
            u3  = length - l;

            if (u2 > 0) {
              q5 = domains_up->exp_energy_cb(fc,
                                             j + 1, k - 1,
                                             VRNA_UNSTRUCTURED_DOMAIN_INT_LOOP,
                                             domains_up->data);
            }

            if (u1 + u3 > 0) {
              q3 = domains_up->exp_energy_cb(fc,
                                             l + 1, i - 1,
                                             VRNA_UNSTRUCTURED_DOMAIN_INT_LOOP,
                                             domains_up->data);
            }

            q += q_temp *
                 q5 *
                 scale[u1 + u2 + u3];
            q += q_temp *
                 q3 *
                 scale[u1 + u2 + u3];
            q += q_temp *
                 q5 *
                 q3 *
                 scale[u1 + u2 + u3];
          }
        }
      }
    }
  }

  free(tt);
  free_sc_wrapper(&sc_wrapper);

  return q;
}


PRIVATE FLT_OR_DBL
exp_E_interior_loop(vrna_fold_compound_t  *vc,
                    int                   i,
                    int                   j,
                    int                   k,
                    int                   l)
{
  unsigned char             sliding_window, type, type2;
  char                      *ptype, **ptype_local;
  unsigned char             *hc_mx, **hc_mx_local, eval_loop, hc_decompose_ij, hc_decompose_kl;
  short                     *S1, **SS, **S5, **S3;
  unsigned int              *sn, n_seq, s, **a2s;
  int                       u1, u2, *rtype, *jindx, *hc_up, ij, kl;
  FLT_OR_DBL                qbt1, q_temp, *scale;
  vrna_exp_param_t          *pf_params;
  vrna_md_t                 *md;
  vrna_ud_t                 *domains_up;
  eval_hc                   *evaluate;
  struct default_data       hc_dat_local;
  struct sc_wrapper_exp_int sc_wrapper;

  sliding_window  = (vc->hc->type == VRNA_HC_WINDOW) ? 1 : 0;
  n_seq           = (vc->type == VRNA_FC_TYPE_SINGLE) ? 1 : vc->n_seq;
  ptype           = (vc->type == VRNA_FC_TYPE_SINGLE) ? (sliding_window ? NULL : vc->ptype) : NULL;
  ptype_local     =
    (vc->type == VRNA_FC_TYPE_SINGLE) ? (sliding_window ? vc->ptype_local : NULL) : NULL;
  S1          = (vc->type == VRNA_FC_TYPE_SINGLE) ? vc->sequence_encoding : NULL;
  SS          = (vc->type == VRNA_FC_TYPE_SINGLE) ? NULL : vc->S;
  S5          = (vc->type == VRNA_FC_TYPE_SINGLE) ? NULL : vc->S5;
  S3          = (vc->type == VRNA_FC_TYPE_SINGLE) ? NULL : vc->S3;
  a2s         = (vc->type == VRNA_FC_TYPE_SINGLE) ? NULL : vc->a2s;
  jindx       = vc->jindx;
  hc_mx       = (sliding_window) ? NULL : vc->hc->matrix;
  hc_mx_local = (sliding_window) ? vc->hc->matrix_local : NULL;
  hc_up       = vc->hc->up_int;
  pf_params   = vc->exp_params;
  sn          = vc->strand_number;
  md          = &(pf_params->model_details);
  scale       = vc->exp_matrices->scale;
  domains_up  = vc->domains_up;
  rtype       = &(md->rtype[0]);
  qbt1        = 0.;
  u1          = k - i - 1;
  u2          = j - l - 1;

  if ((sn[k] != sn[i]) || (sn[j] != sn[l]))
    return qbt1;

  if (hc_up[l + 1] < u2)
    return qbt1;

  if (hc_up[i + 1] < u1)
    return qbt1;

  evaluate = prepare_hc_default(vc, &hc_dat_local);

  init_sc_wrapper(vc, &sc_wrapper);

  ij              = (sliding_window) ? 0 : jindx[j] + i;
  kl              = (sliding_window) ? 0 : jindx[l] + k;
  hc_decompose_ij = (sliding_window) ? hc_mx_local[i][j - i] : hc_mx[ij];
  hc_decompose_kl = (sliding_window) ? hc_mx_local[k][l - k] : hc_mx[kl];
  eval_loop       = ((hc_decompose_ij & VRNA_CONSTRAINT_CONTEXT_INT_LOOP) &&
                     (hc_decompose_kl & VRNA_CONSTRAINT_CONTEXT_INT_LOOP_ENC)) ?
                    1 : 0;

  /* discard this configuration if (p,q) is not allowed to be enclosed pair of an interior loop */
  if (eval_loop && evaluate(i, j, k, l, &hc_dat_local)) {
    q_temp = 0;

    switch (vc->type) {
      case VRNA_FC_TYPE_SINGLE:
        type = (sliding_window) ?
               vrna_get_ptype_window(i, j, ptype_local) :
               vrna_get_ptype(ij, ptype);
        type2 = (sliding_window) ?
                rtype[vrna_get_ptype_window(k, l, ptype_local)] :
                rtype[vrna_get_ptype(jindx[l] + k, ptype)];

        q_temp = exp_E_IntLoop(u1,
                               u2,
                               type,
                               type2,
                               S1[i + 1],
                               S1[j - 1],
                               S1[k - 1],
                               S1[l + 1],
                               pf_params);

        break;

      case VRNA_FC_TYPE_COMPARATIVE:
        q_temp = 1.;

        for (s = 0; s < n_seq; s++) {
          int u1_local  = a2s[s][k - 1] - a2s[s][i];
          int u2_local  = a2s[s][j - 1] - a2s[s][l];
          type    = vrna_get_ptype_md(SS[s][i], SS[s][j], md);
          type2   = vrna_get_ptype_md(SS[s][l], SS[s][k], md);
          q_temp  *= exp_E_IntLoop(u1_local,
                                   u2_local,
                                   type,
                                   type2,
                                   S3[s][i],
                                   S5[s][j],
                                   S5[s][k],
                                   S3[s][l],
                                   pf_params);
        }

        break;
    }

    /* soft constraints */
    if (sc_wrapper.pair)
      q_temp *= sc_wrapper.pair(i, j, k, l, &sc_wrapper);

    qbt1 += q_temp *
            scale[u1 + u2 + 2];

    /* unstructured domains */
    if (domains_up && domains_up->exp_energy_cb) {
      FLT_OR_DBL qq5, qq3;

      qq5 = qq3 = 0.;

      if (u1 > 0) {
        qq5 = domains_up->exp_energy_cb(vc,
                                        i + 1, k - 1,
                                        VRNA_UNSTRUCTURED_DOMAIN_INT_LOOP,
                                        domains_up->data);
      }

      if (u2 > 0) {
        qq3 = domains_up->exp_energy_cb(vc,
                                        l + 1, j - 1,
                                        VRNA_UNSTRUCTURED_DOMAIN_INT_LOOP,
                                        domains_up->data);
      }

      qbt1 += q_temp *
              qq5 *
              scale[u1 + u2 + 2];      /* only motifs in 5' part */
      qbt1 += q_temp *
              qq3 *
              scale[u1 + u2 + 2];      /* only motifs in 3' part */
      qbt1 += q_temp *
              qq5 *
              qq3 *
              scale[u1 + u2 + 2]; /* motifs in both parts */
    }
  }

  free_sc_wrapper(&sc_wrapper);

  return qbt1;
}
