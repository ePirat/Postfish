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
#include <fftw3.h>
#include "subband.h"
#include "bessel.h"
#include "deverb.h"

/* (since this one is kinda unique) The Deverberation Filter....
   
   Reverberation in a measurably live environment displays
   log amplitude decay with time (linear decay when plotted on a dB
   scale).
   
   In its simplest form, the deverber follows actual RMS amplitude
   attacks but chooses a slower-than-actual decay, then expands
   according to the dB distance between the slow and actual decay.
   
   Thus, the deverber can be used to 'dry out' a very 'wet'
   reverberative track. */
    
extern int input_size;
extern int input_rate;
extern int input_ch;

typedef struct {
  subband_state ss;
  
  iir_filter smooth;
  iir_filter smoothlimit;
  iir_filter release;
  
  iir_state *iirS[deverb_freqs];
  iir_state *iirR[deverb_freqs];

  float prevratio[deverb_freqs];

  int inactive_delay[deverb_freqs];

} deverb_state;

deverb_settings deverb_channel_set;
static deverb_state channel_state;
static subband_window sw;

static void deverb_reset_band(int freq){
  int j;
  for(j=0;j<input_ch;j++)
    memset(&channel_state.iirS[freq][j],0,sizeof(iir_state));
  for(j=0;j<input_ch;j++)
    memset(&channel_state.iirR[freq][j],0,sizeof(iir_state));
  channel_state.inactive_delay[freq]=2;
}

static void deverb_reset_one(int freq,int ch){
  memset(&channel_state.iirS[freq][ch],0,sizeof(iir_state));
  memset(&channel_state.iirR[freq][ch],0,sizeof(iir_state));
}

void deverb_reset(){
  int i,j;
  
  subband_reset(&channel_state.ss);
  
  for(i=0;i<deverb_freqs;i++)
      deverb_reset_band(i);
}

static void filter_set(subband_state *ss,
		       float msec,
		       iir_filter *filter,
		       int attackp,
		       int order){
  float alpha;
  float corner_freq= 500./msec;
  
  /* make sure the chosen frequency doesn't require a lookahead
     greater than what's available */
  if(impulse_freq2(input_size*2-ss->qblocksize*3)*1.01>corner_freq && 
     attackp)
    corner_freq=impulse_freq2(input_size*2-ss->qblocksize*3);
  
  alpha=corner_freq/input_rate;
  filter->g=mkbessel(alpha,order,filter->c);
  filter->alpha=alpha;
  filter->Hz=alpha*input_rate;
  filter->ms=msec;
}

int deverb_load(void){
  int i;
  int qblocksize=input_size/16;
  memset(&channel_state,0,sizeof(channel_state));

  deverb_channel_set.active=calloc(input_ch,sizeof(*deverb_channel_set.active));

  subband_load(&channel_state.ss,deverb_freqs,qblocksize,input_ch);
  subband_load_freqs(&channel_state.ss,&sw,deverb_freq_list,deverb_freqs);
   
  for(i=0;i<deverb_freqs;i++){
    channel_state.iirS[i]=calloc(input_ch,sizeof(iir_state));
    channel_state.iirR[i]=calloc(input_ch,sizeof(iir_state));
    channel_state.inactive_delay[i]=2;
  }
  return 0;
}

static void deverb_work_helper(void *vs, deverb_settings *sset){
  deverb_state *sss=(deverb_state *)vs;
  subband_state *ss=&sss->ss;
  int i,j,k,l;
  float smoothms=sset->smooth*.1;
  float releasems=sset->release*.1;
  iir_filter *smooth=&sss->smooth;
  iir_filter *smoothlimit=&sss->smoothlimit;
  iir_filter *release=&sss->release;
  int ahead;

  if(smoothms!=smooth->ms){
    filter_set(ss,smoothms,smooth,1,2);
    filter_set(ss,smoothms,smoothlimit,0,1);
  }
  if(releasems!=release->ms)filter_set(ss,releasems,release,0,1);

  ahead=impulse_ahead2(smooth->alpha);
  
  for(i=0;i<deverb_freqs;i++){
    int firstlink=0;
    float fast[input_size];
    float slow[input_size];
    int ratio=sset->ratio[i];
    float multiplier = (sss->inactive_delay[i]>0 ? 0. : 1.-1000./ratio);

    if(ratio==1000 && sss->prevratio[i]==0.){

      /* although the effect is active, this band is set to unity.
	 Save CPU: Do not run the filters.  This is not tied into the
	 subbanding 'active' indicator as that's by channel, not by
	 band. */

      deverb_reset_band(i);

    }else{
      sss->inactive_delay[i]--;
      if(sss->inactive_delay[i]<0)sss->inactive_delay[i]=0;
      
      for(j=0;j<input_ch;j++){
	int active=(ss->effect_active1[j] || 
		    ss->effect_active0[j] || 
		    ss->effect_activeC[j]);
	
	if(!active){
	  deverb_reset_one(i,j);
	}else{

	  /* run the filters */
	  
	  if(sset->linkp){
	    if(!firstlink){
	      firstlink++;
	      memset(fast,0,sizeof(fast));
	      float scale=1./input_ch;
	      for(l=0;l<input_ch;l++){
		float *x=sss->ss.lap[i][l]+ahead;
		for(k=0;k<input_size;k++)
		  fast[k]+=x[k]*x[k];
	      }
	      for(k=0;k<input_size;k++)
		fast[k]*=scale;
	      
	    }
	    
	  }else{
	    float *x=sss->ss.lap[i][j]+ahead;
	    for(k=0;k<input_size;k++)
	      fast[k]=x[k]*x[k];
	  }
	  
	  
	  if(sset->linkp==0 || firstlink==1){
	    
	    compute_iir_freefall_limited(fast, input_size, &sss->iirS[i][j],
					 smooth,smoothlimit);
	    
	    memcpy(slow,fast,sizeof(slow));
	    compute_iir_freefallonly1(slow, input_size, &sss->iirR[i][j],
				      release);
	    
	    //_analysis("fast3",i,fast,input_size,1,offset);
	    
	    if(multiplier==sss->prevratio[i]){
	      
	      for(k=0;k<input_size;k++)
		fast[k]=fromdB_a((todB_a(slow+k)-todB_a(fast+k))*.5*multiplier);
	      
	    }else{
	      float multiplier_add=(multiplier-sss->prevratio[i])/input_size;
	      multiplier=sss->prevratio[i];
	      
	      for(k=0;k<input_size;k++){
		fast[k]=fromdB_a((todB_a(slow+k)-todB_a(fast+k))*.5*multiplier);
		multiplier+=multiplier_add;
	      }
	      
	    }
	    
	    //_analysis("adj3",i,fast,input_size,1,offset);
	  
	    if(sset->linkp && firstlink==1){
	      
	      for(l=0;l<input_ch;l++){
		if(l!=j){
		  memcpy(&sss->iirS[i][l],&sss->iirS[i][j],sizeof(iir_state));
		  memcpy(&sss->iirR[i][l],&sss->iirR[i][j],sizeof(iir_state));
		}
	      }
	    }
	    
	    firstlink++;
	  }
	  
	  
	  {
	    float *x=sss->ss.lap[i][j];
	    for(k=0;k<input_size;k++)
	      if(fast[k]<1.)
		x[k]*=fast[k];
	  }
	}
      }
    }
    
    sss->prevratio[i]=multiplier;

  }

}

static void deverb_work_channel(void *vs){
  deverb_work_helper(vs,&deverb_channel_set);
}

time_linkage *deverb_read_channel(time_linkage *in){
  int visible[input_ch];
  int active [input_ch];
  subband_window *w[input_ch];
  int i;
  
  for(i=0;i<input_ch;i++){
    visible[i]=0;
    active[i]=deverb_channel_set.active[i];
    w[i]=&sw;
  }
  
  return subband_read(in, &channel_state.ss, w, visible, active, 
		      deverb_work_channel, &channel_state);
}
