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

typedef struct {

  int      freq_bands;
  float  **ho_window;
  
} subband_window;

typedef struct {
  time_linkage out;

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
  float  **cache0;
  float  **cache1;

  int      *lap_activeC;
  int      *lap_active0;
  int      *lap_active1;
  int      *lap_activeP;

  int      *visibleC;
  int      *visible0;
  int      *visible1;

  int      *effect_activeC;
  int      *effect_active0;
  int      *effect_active1;
  int      *effect_activeP;

  u_int32_t mutemaskC;
  u_int32_t mutemask0;
  u_int32_t mutemask1;
  u_int32_t mutemaskP;

  int      lap_samples;
  int      fillstate;     /* 0: uninitialized
			     1: partial prime
			     2: nominal
			     3: eof processed */
  subband_window **wP;
  subband_window **w0;
  subband_window **w1;
  subband_window **wC;

} subband_state;



extern int subband_load(subband_state *f,int bands, int qb,int ch);
extern int subband_load_freqs(subband_state *f,subband_window *w,
			      const float *freq_list,int bands);

extern time_linkage *subband_read(time_linkage *in, subband_state *f,
				  subband_window **w,int *visible, int *active,
				  void (*workfunc)(void *),void *arg);

extern int subband_reset(subband_state *f);


