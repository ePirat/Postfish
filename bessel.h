/*
 *
 *  postfish
 *    
 *      Copyright (C) 2002-2004 Monty
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
extern int input_rate;

#define MAXORDER    2

typedef struct {
  double c[MAXORDER];
  double g;
  int   order;
  float alpha; 
  float Hz; 
  float ms; 
  int   samples;
} iir_filter;

static inline long impulse_ahead2(float alpha){
  return rint(.13f/alpha);
}
static inline long impulse_ahead3(float alpha){
  return rint(.22f/alpha);
}
static inline long impulse_ahead4(float alpha){
  return rint(.32f/alpha);
}

static inline long step_ahead(float alpha){
  return rint(.6f/alpha);
}

static inline float step_freq(long ahead){
  return input_rate*.6f/ahead;
}

static inline float impulse_freq2(long ahead){
  return input_rate*.13f/ahead;
}
static inline float impulse_freq3(long ahead){
  return input_rate*.22f/ahead;
}
static inline float impulse_freq4(long ahead){
  return input_rate*.32f/ahead;
}

typedef struct {
  double x[MAXORDER];
  double y[MAXORDER];
} iir_state;

extern double mkbessel(double raw_alpha,int order,double *ycoeff);


extern void compute_iir_symmetric_limited(float *x, int n, iir_state *is, 
					  iir_filter *attack, iir_filter *limit);

extern void compute_iir_decay_limited(float *x, int n, iir_state *is, 
				      iir_filter *decay, iir_filter *limit);


extern void compute_iir_freefall_limited(float *x, int n, iir_state *is, 
					 iir_filter *attack, iir_filter *limit);

extern void compute_iir_freefallonly1(float *x, int n, iir_state *is, 
				       iir_filter *decay);


extern void compute_iir_over_soft(float *x, int n, iir_state *is, 
				   iir_filter *attack, iir_filter *limit,
				   float knee, float mult, float *adj);

extern void compute_iir_under_soft(float *x, int n, iir_state *is, 
				   iir_filter *attack, iir_filter *limit,
				   float knee, float mult, float *adj);

extern void compute_iir_over_hard(float *x, int n, iir_state *is, 
				  iir_filter *attack, iir_filter *limit,
				  float knee, float mult, float *adj);

extern void compute_iir_under_hard(float *x, int n, iir_state *is, 
				   iir_filter *attack, iir_filter *limit,
				   float knee, float mult, float *adj);

extern void compute_iir_over_soft_del(float *x, int n, iir_state *is, 
					  iir_filter *attack, iir_filter *limit,
					  float knee, float mult, float mult2, 
					  float *adj);

extern void compute_iir_under_soft_del(float *x, int n, iir_state *is, 
					   iir_filter *attack, iir_filter *limit,
					   float knee, float mult, float mult2,
					   float *adj);

extern void compute_iir_over_hard_del(float *x, int n, iir_state *is, 
					  iir_filter *attack, iir_filter *limit,
					  float knee, float mult, float mult2,
					  float *adj);

extern void compute_iir_under_hard_del(float *x, int n, iir_state *is, 
					   iir_filter *attack, iir_filter *limit,
					   float knee, float mult, float mult2,
					   float *adj);

extern void reset_iir(iir_state *is,float value);

