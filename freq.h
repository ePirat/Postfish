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
#include <fftw3.h>

typedef struct {
  time_linkage out;
  feedback_generic_pool feedpool;

  float **fftwf_buffer; // need one for each channel
  fftwf_plan *fftwf_forward;   // need one for each channel
  fftwf_plan *fftwf_backward;  // need one for each channel
  
  int qblocksize;
  int bands;

  float **ho_window;
  float  *ho_area;
  
  float *window;
  float **lap;
  float **cache;
  int cache_samples;
  int fillstate;     /* 0: uninitialized
			1: half-primed
			2: nominal
			3: eof processed */
} freq_state;

extern void freq_transform_work(float *work,freq_state *f);
extern int pull_freq_feedback(freq_state *ff,float **peak,float **rms);
extern int freq_load(freq_state *f,const float *frequencies, int bands);
extern int freq_reset(freq_state *f);
extern time_linkage *freq_read(time_linkage *in, freq_state *f,
			       void (*func)(freq_state *f,
					    float **data,
					    float **peak, float **rms),
			       int bypassp);

extern void freq_metric_work(float *work,freq_state *f,
			     float *sq_mags,float *peak,float *rms);
