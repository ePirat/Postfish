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

#include "bessel.h"

typedef struct {
  int loc;
  float val;
} peak_state;

extern void bi_compand(float *A,float *B,float *adj,
		       float corner,
		       float multiplier,
		       float currmultiplier,
		       float lookahead, 
		       int mode,int softknee,
		       iir_filter *attack, iir_filter *decay,
		       iir_state *iir, peak_state *ps,
		       int active,
		       int over);

extern void full_compand(float *A,float *B,float *adj,
			 float multiplier,float currmultiplier,
			 int mode,
			 iir_filter *attack, iir_filter *decay,
			 iir_state *iir, peak_state *ps,
			 int active);
