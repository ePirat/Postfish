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

#define deverb_freqs 8
static const float deverb_freq_list[deverb_freqs+1]={
  125,250,500,1000,2000,4000,8000,16000,9e10
};

static char * const deverb_freq_labels[deverb_freqs]={
  "125","250","500","1k","2k","4k","8k","16k"
};

typedef struct {
  sig_atomic_t ratio[deverb_freqs];
  sig_atomic_t smooth;
  sig_atomic_t trigger;
  sig_atomic_t release;
  sig_atomic_t linkp;

  sig_atomic_t *active;
  sig_atomic_t panel_visible;
} deverb_settings;

extern void deverb_reset();
extern int deverb_load(void);
extern time_linkage *deverb_read_channel(time_linkage *in);

extern deverb_settings deverb_channel_set;

