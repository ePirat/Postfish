/*
 *
 *  postfish
 *    
 *      Copyright (C) 2002-2005 Monty and Xiph.Org
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

#define multicomp_freqs_max 20
#define multicomp_banks 3

static const int multicomp_freqs[multicomp_banks]={10,20,5};
static const float multicomp_freq_list[multicomp_banks][multicomp_freqs_max+1]={

  {31.5,63,125,250,500,1000,2000,4000,8000,16000,9e10},

  {31.5,44,63,88,125,175,250,350,500,700,1000,1400,
     2000,2800,4000,5600,8000,11000,16000,20000,9e10},

  {31.5,125,500,2000,8000,9e10},

};

static char * const multicomp_freq_labels[multicomp_banks][multicomp_freqs_max]={
  {"31.5","63","125","250","500","1k","2k","4k","8k","16k"},
  {"31.5","44","63","88","125","175","250","350","500","700","1k","1.4k",
   "2k","2.8k","4k","5.6k","8k","11k","16k","20k"},
  {"31.5","125","500","2k","8k"},
};

typedef struct {
  sig_atomic_t static_o[multicomp_freqs_max];
  sig_atomic_t static_u[multicomp_freqs_max];
} banked_multicompand_settings;

typedef struct {

  banked_multicompand_settings bc[multicomp_banks];

  sig_atomic_t over_mode;
  sig_atomic_t over_softknee;
  sig_atomic_t over_ratio;
  sig_atomic_t over_attack;
  sig_atomic_t over_decay;
  sig_atomic_t over_lookahead;

  sig_atomic_t base_mode;
  sig_atomic_t base_attack;
  sig_atomic_t base_decay;
  sig_atomic_t base_ratio;

  sig_atomic_t under_mode;
  sig_atomic_t under_softknee;
  sig_atomic_t under_ratio;
  sig_atomic_t under_attack;
  sig_atomic_t under_decay;
  sig_atomic_t under_lookahead;

  sig_atomic_t active_bank;

  sig_atomic_t panel_active;
  sig_atomic_t panel_visible;
} multicompand_settings;

extern void multicompand_reset();
extern int multicompand_load(int outch);

extern time_linkage *multicompand_read_channel(time_linkage *in);
extern time_linkage *multicompand_read_master(time_linkage *in);

extern int pull_multicompand_feedback_channel(float **peak,float **rms,int *bands);
extern int pull_multicompand_feedback_master(float **peak,float **rms,int *bands);

extern multicompand_settings multi_master_set;
extern multicompand_settings *multi_channel_set;


