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

#define multicomp_freqs 9

static float multicomp_freq_list[multicomp_freqs+1]={
  63,
  125,
  250,
  500,
  1000,
  2000,
  4000,
  8000,
  16000,9e10};

static char *multicomp_freq_labels[multicomp_freqs]={
  "63",
  "125",
  "250",
  "500",
  "1k",
  "2k",
  "4k",
  "8k",
  "16k"
};

typedef struct {
  sig_atomic_t link_mode;

  sig_atomic_t static_mode;
  sig_atomic_t static_trim;
  sig_atomic_t static_decay;
  sig_atomic_t static_c_ratio;
  sig_atomic_t static_e_ratio;
  sig_atomic_t static_g[multicomp_freqs];
  sig_atomic_t static_e[multicomp_freqs];
  sig_atomic_t static_c[multicomp_freqs];

  sig_atomic_t envelope_mode;
  sig_atomic_t envelope_c;

  sig_atomic_t suppress_mode;
  sig_atomic_t suppress_ratio;
  sig_atomic_t suppress_decay;
  sig_atomic_t suppress_bias;
  sig_atomic_t suppress_depth;

} compand_settings;

extern void multicompand_reset();
extern int multicompand_load(void);
extern time_linkage *multicompand_read(time_linkage *in);


