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
#include "multicompand.h"
#include <fftw3.h>
#include "subband.h"
#include "bessel.h"

extern int input_size;
extern int input_rate;
extern int input_ch;

typedef struct {
  int loc;
  float val;
} peak_state;

typedef struct {
  subband_state ss;
  subband_window sw[multicomp_banks];
  
  iir_filter over_attack[multicomp_banks][multicomp_freqs_max];
  iir_filter over_decay[multicomp_banks][multicomp_freqs_max];

  iir_filter under_attack[multicomp_banks][multicomp_freqs_max];
  iir_filter under_decay[multicomp_banks][multicomp_freqs_max];

  iir_filter base_attack[multicomp_banks][multicomp_freqs_max];
  iir_filter base_decay[multicomp_banks][multicomp_freqs_max];

  iir_state *over_iir[multicomp_freqs_max];
  iir_state *under_iir[multicomp_freqs_max];
  iir_state *base_iir[multicomp_freqs_max];

  peak_state *over_peak[multicomp_freqs_max];
  peak_state *under_peak[multicomp_freqs_max];
  peak_state *base_peak[multicomp_freqs_max];
  
  sig_atomic_t pending_bank;
  sig_atomic_t active_bank;

} multicompand_state;

sig_atomic_t compand_visible;
sig_atomic_t compand_active;

banked_compand_settings bc[multicomp_banks];
other_compand_settings c;

static multicompand_state ms;

int pull_multicompand_feedback(float **peak,float **rms,int *bands){
  return pull_subband_feedback(&ms.ss,peak,rms,bands);
}

void multicompand_reset(){
  int i,j;
  
  subband_reset(&ms.ss);
  
  for(i=0;i<multicomp_freqs_max;i++)
    for(j=0;j<input_ch;j++){
      memset(&ms.over_peak[i][j],0,sizeof(peak_state));
      memset(&ms.under_peak[i][j],0,sizeof(peak_state));
      memset(&ms.base_peak[i][j],0,sizeof(peak_state));
      memset(&ms.over_iir[i][j],0,sizeof(iir_state));
      memset(&ms.under_iir[i][j],0,sizeof(iir_state));
      memset(&ms.base_iir[i][j],0,sizeof(iir_state));
    }
}

int multicompand_load(void){
  int h,i;
  int qblocksize=input_size/8;
  memset(&ms,0,sizeof(ms));

  subband_load(&ms.ss,multicomp_freqs_max,qblocksize);

  for(h=0;h<multicomp_banks;h++)
    subband_load_freqs(&ms.ss,&ms.sw[h],multicomp_freq_list[h],
		       multicomp_freqs[h]);
    
  for(i=0;i<multicomp_freqs_max;i++){
    ms.over_peak[i]=calloc(input_ch,sizeof(peak_state));
    ms.under_peak[i]=calloc(input_ch,sizeof(peak_state));
    ms.base_peak[i]=calloc(input_ch,sizeof(peak_state));
    ms.over_iir[i]=calloc(input_ch,sizeof(iir_state));
    ms.under_iir[i]=calloc(input_ch,sizeof(iir_state));
    ms.base_iir[i]=calloc(input_ch,sizeof(iir_state));
  }

  ms.active_bank=0;

  return 0;
}

static void multicompand_filter_set(float msec,
				   iir_filter *filter,
				   int attackp){
  float alpha;
  float corner_freq= 500./msec;

  /* make sure the chosen frequency doesn't require a lookahead
     greater than what's available */
  if(step_freq(input_size*2-ms.ss.qblocksize*3)*1.01>corner_freq && attackp)
    corner_freq=step_freq(input_size*2-ms.ss.qblocksize*3);

  alpha=corner_freq/input_rate;
  filter->g=mkbessel(alpha,2,filter->c);
  filter->alpha=alpha;
  filter->Hz=alpha*input_rate;
}


static int multicompand_filterbank_set(float msec,
				       iir_filter 
				       filterbank[multicomp_banks]
				       [multicomp_freqs_max],
				       int attackp){
  int i,j;
  for(j=0;j<multicomp_banks;j++){
    int bands=multicomp_freqs[j];
    iir_filter *filters=filterbank[j];
    
    for(i=0;i<bands;i++)
      multicompand_filter_set(msec,filters+i,attackp);
  }
  return 0;
}

int multicompand_over_attack_set(float msec){
  return multicompand_filterbank_set(msec,ms.over_attack,1);
}

int multicompand_over_decay_set(float msec){
  return multicompand_filterbank_set(msec,ms.over_decay,0);  
}

int multicompand_under_attack_set(float msec){
  return multicompand_filterbank_set(msec,ms.under_attack,1);
}

int multicompand_under_decay_set(float msec){
  return multicompand_filterbank_set(msec,ms.under_decay,0);
}

int multicompand_base_attack_set(float msec){
  return multicompand_filterbank_set(msec,ms.base_attack,1);
}

int multicompand_base_decay_set(float msec){
  return multicompand_filterbank_set(msec,ms.base_decay,0);
}

static void prepare_rms(float *rms, float *xx, int n, int ahead){
  int i;
  float *x=xx+ahead;
  for(i=0;i<n;i++)
    rms[i]+=x[i]*x[i];
}

static void prepare_peak(float *peak, float *x, int n, int ahead,int hold,
			 peak_state *ps){
  int ii,jj;
  int loc=ps->loc;
  float val=ps->val;

  /* Although we have two input_size blocks of zeroes after a
     reset, we may still need to look ahead explicitly after a
     reset if the lookahead is exceptionally long */
  if(loc==0 && val==0){
    for(ii=0;ii<ahead;ii++) 
      if(fabs(x[ii])>val){
	val=fabs(x[ii]);
	loc=ii+hold;
      }
  }
  
  if(val>peak[0])peak[0]=val;
  
  for(ii=1;ii<n;ii++){
    if(fabs(x[ii+ahead])>val){
      val=fabs(x[ii+ahead]);
      loc=ii+ahead+hold;
    }	  
    if(ii>=loc){
      /* backfill */
      val=0;
      for(jj=ii+ahead-1;jj>=ii;jj--){
	if(fabs(x[jj])>val)val=fabs(x[jj]);
	if(jj<n && val>peak[jj])peak[jj]=val;
      }
      val=fabs(x[ii+ahead-1]);
      loc=ii+ahead+hold;
    }
    if(val>peak[ii])peak[ii]=val; 
  }

  ps->loc=loc-input_size;
  ps->val=val;
  
}

static void run_filter_only(float *dB,int n,int mode,
			    iir_state *iir,iir_filter *attack,iir_filter *decay){
  int i;
  compute_iir2(dB, n, iir, attack, decay);
  
  if(mode==0)
    for(i=0;i<n;i++)
      dB[i]=todB_a(dB+i)*.5f;
  else
    for(i=0;i<n;i++)
      dB[i]=todB_a(dB+i);

}

static void run_filter(float *dB,float *x,int n,
		       float lookahead,int mode,
		       iir_state *iir,iir_filter *attack,iir_filter *decay,
		       peak_state *ps){

  memset(dB,0,sizeof(*dB)*n);
  
  if(mode)
    prepare_peak(dB, x, n,
		 step_ahead(attack->alpha)*lookahead, 
		 step_ahead(attack->alpha)*(1.-lookahead),
		 ps);
  else
    prepare_rms(dB, x, n, impulse_ahead2(attack->alpha)*lookahead);

  
  run_filter_only(dB,n,mode,iir,attack,decay);
}

static float soft_knee(float x){
  return (sqrtf(x*x+30.f)+x)*-.5f;
}

static float hard_knee(float x){
  return (x>0.f?-x:0.f);
}

static void over_compand(float *lx,float zerocorner,
			 iir_filter *attack, iir_filter *decay,
			 iir_state *iir, peak_state *ps,
			 float *peakfeed, float *rmsfeed,float *adj,int active){
  int k;
  float overdB[input_size];
  float lookahead=c.over_lookahead/1000.;
  int mode=c.over_mode;
  float corner_multiplier=(1.-1./(c.over_ratio/1000.));

  run_filter(overdB,lx,input_size,lookahead,mode,iir,attack, decay,ps);
  
  if(active){
    if(c.over_softknee){
      for(k=0;k<input_size;k++)
	adj[k]+=soft_knee(overdB[k]-zerocorner)*corner_multiplier;
    }else{
      for(k=0;k<input_size;k++)
	adj[k]+=hard_knee(overdB[k]-zerocorner)*corner_multiplier;
    }
  }
  
  {
    /* determine rms and peak for feedback */
    float max=-1.;
    int maxpos=-1;
    float rms=0.;
    
    for(k=0;k<input_size;k++){
      float val=lx[k]*lx[k];
      if(val>max){
	max=val;
	maxpos=k;
      }
      rms+=val;
    }
    *peakfeed=todB(max)*.5+adj[maxpos];
    *rmsfeed=todB(rms/input_size)*.5+adj[maxpos];
  }
}

static void base_compand(float *x,
			 iir_filter *attack, iir_filter *decay,
			 iir_state *iir, peak_state *ps,
			 float *adj,int active){
  int k;
  float basedB[input_size];
  int mode=c.base_mode;
  float base_multiplier=(1.-1./(c.base_ratio/1000.));

  run_filter(basedB,x,input_size,1.,mode,
	     iir,attack,decay,ps);
  
  if(active)
    for(k=0;k<input_size;k++)
      adj[k]-=(basedB[k]+adj[k])*base_multiplier;
  
}

static void under_compand(float *x,float zerocorner,
			 iir_filter *attack, iir_filter *decay,
			 iir_state *iir, peak_state *ps,
			 float *adj,int active){
  int k;
  float underdB[input_size];
  float lookahead=c.under_lookahead/1000.;
  int mode=c.under_mode;
  float corner_multiplier=(1.-1./(c.under_ratio/1000.));

  run_filter(underdB,x,input_size,lookahead,mode,
	     iir,attack,decay,ps);
  
  if(active){
    if(c.under_softknee){
      for(k=0;k<input_size;k++)
	adj[k]-=soft_knee(zerocorner-underdB[k])*corner_multiplier;
    }else{
      for(k=0;k<input_size;k++)
	adj[k]-=hard_knee(zerocorner-underdB[k])*corner_multiplier;
    }
  }
}

static void multicompand_work(float **peakfeed,float **rmsfeed){
  int i,j,k;
  float adj[input_size];
  float *x;
  int active=compand_active;

  for(i=0;i<multicomp_freqs[ms.active_bank];i++){

    for(j=0;j<input_ch;j++){
      memset(adj,0,sizeof(adj));

      if(active || compand_visible){
	under_compand(ms.ss.lap[i][j],  
		      bc[ms.active_bank].static_u[i],
		      &ms.under_attack[ms.active_bank][i],
		      &ms.under_decay[ms.active_bank][i],
		      &ms.under_iir[i][j],
		      &ms.under_peak[i][j],
		      adj,active);
	
	over_compand(ms.ss.lap[i][j],  
		     bc[ms.active_bank].static_o[i],
		     &ms.over_attack[ms.active_bank][i],
		     &ms.over_decay[ms.active_bank][i],
		     &ms.over_iir[i][j],
		     &ms.over_peak[i][j],
		     &peakfeed[i][j],
		     &rmsfeed[i][j],
		     adj,active);
	
	base_compand(ms.ss.lap[i][j],  
		     &ms.base_attack[ms.active_bank][i],
		     &ms.base_decay[ms.active_bank][i],
		     &ms.base_iir[i][j],
		     &ms.base_peak[i][j],
		     adj,active);
      }

      
      if(active){
	x=ms.ss.lap[i][j];
	for(k=0;k<input_size;k++)
	  x[k]*=fromdB_a(adj[k]);
      }
    }
  }
}

void multicompand_set_bank(int bank){
  ms.pending_bank=bank;
}

time_linkage *multicompand_read(time_linkage *in){
  int bypass=!(compand_visible||compand_active);
  
  ms.active_bank=ms.pending_bank;

  return subband_read(in,&ms.ss,&ms.sw[ms.active_bank],
		      multicompand_work,bypass);
}

