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
#include "feedback.h"
#include "freq.h"
#include "eq.h"

extern int input_size;

sig_atomic_t eq_active;

freq_state eq;

int pull_eq_feedback(double **peak,double **rms){
  return pull_freq_feedback(&eq,peak,rms);
}

/* called only by initial setup */
int eq_load(void){
  return freq_load(&eq,input_size);
}

/* called only in playback thread */
int eq_reset(){
  return freq_reset(&eq);
}

static void workfunc(double *data,freq_state *f,
		     double *peak, double *rms){

  return;
}

/* called only by playback thread */
time_linkage *eq_read(time_linkage *in){
  return freq_read(in,&eq,workfunc);
}
