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

#define eq_freqs 30

typedef struct {
  sig_atomic_t settings[eq_freqs];
  sig_atomic_t panel_active;
  sig_atomic_t panel_visible;
  sig_atomic_t curve_dirty;
  float *curve_cache; 

} eq_settings;

static const float eq_freq_list[eq_freqs+1]={
  25,31.5,40,50,63,80,
  100,125,160,200,250,315,
  400,500,630,800,1000,1250,1600,
  2000,2500,3150,4000,5000,6300,
  8000,10000,12500,16000,20000,9e10};

static char * const eq_freq_labels[eq_freqs]={
  "25","31.5","40","50","63","80",
  "100","125","160","200","250","315",
  "400","500","630","800","1k","1.2k","1.6k",
  "2k","2.5k","3.1k","4k","5k","6.3k",
  "8k","10k","12.5k","16k","20k"
};

extern int pull_eq_feedback_master(float **peak,float **rms);
extern int pull_eq_feedback_channel(float **peak,float **rms);
extern int eq_load(void);
extern int eq_reset();
extern void eq_set(eq_settings *eq,int freq, float value);
extern time_linkage *eq_read_master(time_linkage *in);
extern time_linkage *eq_read_channel(time_linkage *in);
