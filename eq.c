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

sig_atomic_t curve_dirty=1;

freq_state eq;

/* accessed only in playback thread/setup */
int pull_eq_feedback(float **peak,float **rms){
  return pull_freq_feedback(&eq,peak,rms);
}

/* called only by initial setup */
int eq_load(void){
  return freq_load(&eq,eq_freq_list,eq_freqs);
}

/* called only in playback thread */
int eq_reset(){
  return freq_reset(&eq);
}

static sig_atomic_t settings[eq_freqs];

void eq_set(int freq, float value){
  settings[freq]=rint(value*10.);
  curve_dirty=1;
}
 
static float *curve_cache=0;
 
static void workfunc(freq_state *f,float **data,float **peak, float **rms){
  int h,i,j;
  float sq_mags[f->qblocksize*2+1];
  
  if(curve_dirty || !curve_cache){
    curve_dirty=0;
    
    if(!curve_cache)curve_cache=malloc((f->qblocksize*2+1)*sizeof(*curve_cache));
    memset(curve_cache,0,(f->qblocksize*2+1)*sizeof(*curve_cache));
    
    for(i=0;i<eq_freqs;i++){
      float set=fromdB(settings[i]*.1);
      for(j=0;j<f->qblocksize*2+1;j++)
	curve_cache[j]+=f->ho_window[i][j]*set;
    }
  }
  
  for(h=0;h<input_ch;h++){
    
    if(eq_active){
      
      for(i=0;i<f->qblocksize*2+1;i++){
	data[h][i*2]*=curve_cache[i];
	data[h][i*2+1]*=curve_cache[i];
      }
    }
    
    freq_metric_work(data[h],f,sq_mags,peak[h],rms[h]);
  }

  return;
}

/* called only by playback thread */
time_linkage *eq_read(time_linkage *in){
  return freq_read(in,&eq,workfunc,!(eq_visible||eq_active));
}
