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
  time_linkage out;
  feedback_generic_pool feedpool;

  float *fftwf_forward_out;
  float *fftwf_backward_in;
  float *fftwf_forward_in;
  float *fftwf_backward_out;
  fftwf_plan fftwf_forward;
  fftwf_plan fftwf_backward;
  
  int      qblocksize;
  float   *window;
  int      bands;
  float ***lap;
  float  **cache;
  int      lap_samples;
  int      fillstate;     /* 0: uninitialized
			     1: partial prime
			     2: nominal
			     3: eof processed */
  int lapbands[3];
} subband_state;

typedef struct {

  int      freq_bands;
  float  **ho_window;
  float   *ho_area;
  
} subband_window;



extern int subband_load(subband_state *f,int bands, int qb);
extern int subband_load_freqs(subband_state *f,subband_window *w,
			      const float *freq_list,int bands);

extern time_linkage *subband_read(time_linkage *in, subband_state *f,
				  subband_window *w,
				  void (*func)(float **, float **),
				  int bypass);

extern int subband_reset(subband_state *f);
extern int pull_subband_feedback(subband_state *ff,
				 float **peak,float **rms,int *bands);


