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
  float       *fftwf_buffer; 
  fftwf_plan  fftwf_forward; 
  fftwf_plan  fftwf_backward;
  
  int qblocksize;
  int bands;

  float **ho_window;
  float  *ho_area;
  
  float *window;
} freq_class_setup;


typedef struct {
  time_linkage out;
  feedback_generic_pool feedpool;
  freq_class_setup *fc;

  int *activeP;
  int *active1;
  int *active0;

  u_int32_t mutemask0;
  u_int32_t mutemask1;
  u_int32_t mutemaskP;

  float **lap1;
  float **lap0;
  float **lapC;

  float **cache1;
  float **cache0;
  int cache_samples;
  int fillstate;     /* 0: uninitialized
			1: half-primed
			2: nominal
			3: eof processed */
  float **peak;
  float **rms;
} freq_state;



extern int pull_freq_feedback(freq_state *ff,float **peak,float **rms);
extern int freq_class_load(freq_class_setup *f,const float *frequencies, int bands);
extern int freq_load(freq_state *f,freq_class_setup *fc,int ch);

extern int freq_reset(freq_state *f);
extern time_linkage *freq_read(time_linkage *in, 
			       freq_state *f, 
			       int *visible, int *active,
			       void (*func)(float *,int i));
