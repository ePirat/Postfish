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
#include "suppress.h"

/* (since this one is kinda unique) The Reverberation Suppressor....
   
   Reverberation in a measurably live environment displays
   log amplitude decay with time (linear decay when plotted on a dB
   scale).
   
   In its simplest form, the suppressor follows actual RMS amplitude
   attacks but chooses a slower-than-actual decay, then expands
   according to the dB distance between the slow and actual decay.
   
   Thus, the suppressor can be used to 'dry out' a very 'wet'
   reverberative track. */
    
extern int input_size;
extern int input_rate;
extern int input_ch;

typedef struct {
  subband_state ss;
  subband_window sw;
  
  iir_filter smooth;
  iir_filter trigger;
  iir_filter release;
  
  iir_state *iirS[suppress_freqs];
  iir_state *iirT[suppress_freqs];
  iir_state *iirR[suppress_freqs];

} suppress_state;

sig_atomic_t suppress_visible;
sig_atomic_t suppress_active;

suppress_settings sset;
static suppress_state sss;

void suppress_reset(){
  int i,j;
  
  subband_reset(&sss.ss);
  
  for(i=0;i<suppress_freqs;i++){
    for(j=0;j<input_ch;j++){
      memset(&sss.iirS[i][j],0,sizeof(iir_state));
      memset(&sss.iirT[i][j],0,sizeof(iir_state));
      memset(&sss.iirR[i][j],0,sizeof(iir_state));
    }
  }
}

static void filter_set(float msec,
		       iir_filter *filter,
		       int attackp,
		       int order){
  float alpha;
  float corner_freq= 500./msec;
  
  /* make sure the chosen frequency doesn't require a lookahead
     greater than what's available */
  if(impulse_freq4(input_size*2-sss.ss.qblocksize*3)*1.01>corner_freq && 
     attackp)
    corner_freq=impulse_freq4(input_size*2-sss.ss.qblocksize*3);
  
  alpha=corner_freq/input_rate;
  filter->g=mkbessel(alpha,order,filter->c);
  filter->alpha=alpha;
  filter->Hz=alpha*input_rate;
  filter->ms=msec;
}

int suppress_load(void){
  int i;
  int qblocksize=input_size/16;
  memset(&sss,0,sizeof(sss));

  subband_load(&sss.ss,suppress_freqs,qblocksize);
  subband_load_freqs(&sss.ss,&sss.sw,suppress_freq_list,suppress_freqs);
   
  for(i=0;i<suppress_freqs;i++){
    sss.iirS[i]=calloc(input_ch,sizeof(iir_state));
    sss.iirT[i]=calloc(input_ch,sizeof(iir_state));
    sss.iirR[i]=calloc(input_ch,sizeof(iir_state));
  }
  return 0;
}

static void suppress_work(float **peakfeed,float **rmsfeed){
  int i,j,k,l;
  float smoothms=sset.smooth*.1;
  float triggerms=sset.trigger*.1;
  float releasems=sset.release*.1;
  iir_filter *trigger=&sss.trigger;
  iir_filter *smooth=&sss.smooth;
  iir_filter *release=&sss.release;
  int ahead;

  if(smoothms!=smooth->ms)filter_set(smoothms,smooth,1,4);
  if(triggerms!=trigger->ms)filter_set(triggerms,trigger,0,1);
  if(releasems!=release->ms)filter_set(releasems,release,0,1);

  ahead=impulse_ahead4(smooth->alpha);
  
  for(i=0;i<suppress_freqs;i++){

    if(suppress_active){
      float fast[input_size];
      float slow[input_size];
      float multiplier = 1.-1000./sset.ratio[i];

      for(j=0;j<input_ch;j++){
	if(sset.linkp){
	  if(j==0){
	    memset(fast,0,sizeof(fast));
	    float scale=1./input_ch;
	    for(l=0;l<input_ch;l++){
	      float *x=sss.ss.lap[i][l]+ahead;
	      for(k=0;k<input_size;k++)
		fast[k]+=x[k]*x[k];
	    }
	    for(k=0;k<input_size;k++)
	      fast[k]*=scale;

	    //_analysis("rms",i,fast,input_size,0,offset);

	  }
	  
	}else{
	  float *x=sss.ss.lap[i][j]+ahead;
	  for(k=0;k<input_size;k++)
	    fast[k]=x[k]*x[k];
	}

	
	if(j==0 || sset.linkp==0){

	  compute_iir_symmetric4(fast, input_size, &sss.iirS[i][j],
				smooth);
	  
	  //_analysis("smooth",i,fast,input_size,1,offset);
	  
	  compute_iir_freefall1(fast, input_size, &sss.iirT[i][j],
	  		 trigger);
	  memcpy(slow,fast,sizeof(slow));
	  compute_iir_freefall1(slow, input_size, &sss.iirR[i][j],
			       release);
	  
	  //_analysis("fast",i,fast,input_size,1,offset);
	  //_analysis("slow",i,slow,input_size,1,offset);
	  for(k=0;k<input_size;k++)
	    fast[k]=fromdB_a((todB_a(slow+k)-todB_a(fast+k))*.5*multiplier);
	  //_analysis("adj",i,fast,input_size,1,offset);
	}


	{
	  float *x=sss.ss.lap[i][j];
	  for(k=0;k<input_size;k++)
	    if(fast[k]<1.)
	      x[k]*=fast[k];
	}
      }
    }
  }
}

time_linkage *suppress_read(time_linkage *in){
  int bypass=!(suppress_active);
  
  return subband_read(in,&sss.ss,&sss.sw,
		      suppress_work,bypass);
}


