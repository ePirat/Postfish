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
#include "smallft.h"

#define freqs 30

typedef struct {
  drft_lookup fft;
  time_linkage out;
  feedback_generic_pool feedpool;
  
  int blocksize;
  double **ho_window;
  double   ho_area[freqs];
  int      ho_bin_lo[freqs];
  int      ho_bin_hi[freqs];
  
  double *window;
  double **lap;
  double **cache;
  int cache_samples;
  int fillstate;     /* 0: uninitialized
			1: normal
			2: eof processed */
} freq_state;

extern int pull_freq_feedback(freq_state *ff,double **peak,double **rms);
extern int freq_load(freq_state *f,int blocksize);
extern int freq_reset(freq_state *f);
extern const char *freq_frequency_label(int n);
extern time_linkage *freq_read(time_linkage *in, freq_state *f,
			       void (*func)(double *data,freq_state *f,
					    double *peak, double *rms));

extern void freq_metric_work(double *work,freq_state *f,
			    double *sq_mags,double *peak,double *rms);
