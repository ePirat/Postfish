/*
 *
 *  postfish
 *    
 *      Copyright (C) 2002-2004 Monty and Xiph.Org
 *
 *  Postfish is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  Postfish is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with Postfish; see the file COPYING.  If not, write to the
 *  Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * 
 */

#include "postfish.h"

typedef struct {
  sig_atomic_t o_attack;
  sig_atomic_t o_decay;
  sig_atomic_t u_attack;
  sig_atomic_t u_decay;
  sig_atomic_t b_attack;
  sig_atomic_t b_decay;

  sig_atomic_t o_thresh;
  sig_atomic_t o_ratio;
  sig_atomic_t o_lookahead;
  sig_atomic_t o_mode;
  sig_atomic_t o_softknee;

  sig_atomic_t u_thresh;
  sig_atomic_t u_ratio;
  sig_atomic_t u_lookahead;
  sig_atomic_t u_mode;
  sig_atomic_t u_softknee;

  sig_atomic_t b_ratio;
  sig_atomic_t b_mode;

} singlecomp_settings;

extern int pull_singlecomp_feedback(float *peak,float *rms);
extern int singlecomp_load(void);
extern int singlecomp_reset(void);
extern time_linkage *singlecomp_read(time_linkage *in);
