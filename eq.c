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

typedef struct{

  freq_state eq;

} eq_state;

eq_settings eq_master_set;
eq_settings *eq_channel_set;

static freq_class_setup fc;
static eq_state master_state;
static eq_state channel_state;


/* accessed only in playback thread/setup */
int pull_eq_feedback_master(float **peak,float **rms){
  return pull_freq_feedback(&master_state.eq,peak,rms);
}

int pull_eq_feedback_channel(float **peak,float **rms){
  return pull_freq_feedback(&channel_state.eq,peak,rms);
}

/* called only by initial setup */
int eq_load(void){
  int i;

  eq_channel_set=calloc(input_ch,sizeof(*eq_channel_set));

  freq_class_load(&fc,eq_freq_list,eq_freqs);

  freq_load(&master_state.eq,&fc);
  freq_load(&channel_state.eq,&fc);

  eq_master_set.curve_dirty=1;

  for(i=0;i<input_ch;i++)
    eq_channel_set[i].curve_dirty=1;

  return 0;
}

/* called only in playback thread */
int eq_reset(){
  freq_reset(&master_state.eq);
  freq_reset(&channel_state.eq);
  return 0;
}

void eq_set(eq_settings *set, int freq, float value){
  set->settings[freq]=rint(value*10.);
  set->curve_dirty=1;
}
 
static void workfunc(float *data, eq_settings *set){
  int i,j;
  
  if(set->curve_dirty || !set->curve_cache){
    set->curve_dirty=0;
    
    if(!set->curve_cache)
      set->curve_cache=malloc((fc.qblocksize*2+1)*sizeof(*set->curve_cache));
    memset(set->curve_cache,0,(fc.qblocksize*2+1)*sizeof(*set->curve_cache));
    
    for(i=0;i<eq_freqs;i++){
      float v=fromdB_a(set->settings[i]*.1);
      for(j=0;j<fc.qblocksize*2+1;j++)
	set->curve_cache[j]+=fc.ho_window[i][j]*v;
    }
  }
  
  for(i=0;i<fc.qblocksize*2+1;i++){
    data[i*2]*=set->curve_cache[i];
    data[i*2+1]*=set->curve_cache[i];
  }
  
  return;
}

static void workfunc_ch(float *data, int ch){
  workfunc(data,eq_channel_set+ch);
}

static void workfunc_m(float *data, int ch){
  workfunc(data,&eq_master_set);
}

/* called only by playback thread */
time_linkage *eq_read_master(time_linkage *in){
  int active[input_ch];
  int visible[input_ch];
  int i;
  
  for(i=0;i<input_ch;i++){
    active[i]=eq_master_set.panel_active;
    visible[i]=eq_master_set.panel_visible;
  }

  return freq_read(in,&master_state.eq,visible,active,workfunc_m);
}

time_linkage *eq_read_channel(time_linkage *in){
  int active[input_ch];
  int visible[input_ch];
  int i;
  
  for(i=0;i<input_ch;i++){
    active[i]=eq_channel_set[i].panel_active;
    visible[i]=eq_channel_set[i].panel_visible;
  }

  return freq_read(in,&master_state.eq,visible,active,workfunc_ch);
}

