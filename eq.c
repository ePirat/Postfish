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

#include <sys/types.h>
#include "postfish.h"
#include "feedback.h"
#include "freq.h"
#include "eq.h"

extern int input_size;

sig_atomic_t eq_active;
sig_atomic_t eq_visible;

freq_state eq;

int pull_eq_feedback(double **peak,double **rms){
  return pull_freq_feedback(&eq,peak,rms);
}

/* called only by initial setup */
int eq_load(void){
  return freq_load(&eq,input_size*2);
}

/* called only in playback thread */
int eq_reset(){
  return freq_reset(&eq);
}

static sig_atomic_t settings[freqs];

void eq_set(int freq, double value){
  settings[freq]=rint(value*10.);
}
  
static void workfunc(double *data,freq_state *f,
		     double *peak, double *rms){
  int i,j,k;
  double work[f->blocksize+1];
  double sq_mags[f->blocksize+1];

  if(eq_active){
    memset(work,0,sizeof(work));
    
    for(i=0;i<freqs;i++){
      double set=fromdB(settings[i]*.1);
      for(k=0,j=f->ho_bin_lo[i];j<f->ho_bin_hi[i];j++,k++)
	work[j]+=f->ho_window[i][k]*set;
      peak[i]*=set;
      rms[i]*=set;
    }
    
    data[0]*=work[0];
    data[f->blocksize*2-1]*=work[f->blocksize];
    for(i=1;i<f->blocksize;i++){
      data[i*2]*=work[i];
      data[i*2-1]*=work[i];
    }
  }

  freq_metric_work(data,f,sq_mags,peak,rms);

  return;
}

/* called only by playback thread */
time_linkage *eq_read(time_linkage *in){
  return freq_read(in,&eq,workfunc,!(eq_visible||eq_active));
}
