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

#define suppress_freqs 8
static const float suppress_freq_list[suppress_freqs+1]={
  125,250,500,1000,2000,4000,8000,16000,9e10
};

static char * const suppress_freq_labels[suppress_freqs]={
  "125","250","500","1k","2k","4k","8k","16k"
};

typedef struct {
  sig_atomic_t ratio[suppress_freqs];
  sig_atomic_t smooth;
  sig_atomic_t trigger;
  sig_atomic_t release;
  sig_atomic_t linkp;
} suppress_settings;

extern void suppress_reset();
extern int suppress_load(void);
extern time_linkage *suppress_read(time_linkage *in);

