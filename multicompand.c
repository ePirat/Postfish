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

/* feedback! */
typedef struct multicompand_feedback{
  feedback_generic parent_class;
  float **peak;
  float **rms;
  int freq_bands;
  int bypass;
} multicompand_feedback;

typedef struct {
  int loc;
  float val;
} peak_state;

typedef struct {
  feedback_generic_pool feedpool;
  subband_state ss;
  subband_window sw[multicomp_banks];
  
  iir_filter over_attack;
  iir_filter over_decay;

  iir_filter under_attack;
  iir_filter under_decay;

  iir_filter base_attack;
  iir_filter base_decay;

  iir_state *over_iir[multicomp_freqs_max];
  iir_state *under_iir[multicomp_freqs_max];
  iir_state *base_iir[multicomp_freqs_max];

  peak_state *over_peak[multicomp_freqs_max];
  peak_state *under_peak[multicomp_freqs_max];
  peak_state *base_peak[multicomp_freqs_max];
  
  float **peak;
  float **rms;
} multicompand_state;

sig_atomic_t compand_visible;
sig_atomic_t compand_active;

banked_compand_settings bc[multicomp_banks];
other_compand_settings c;

static multicompand_state ms;

static feedback_generic *new_multicompand_feedback(void){
  int i;
  multicompand_feedback *ret=calloc(1,sizeof(*ret));

  ret->peak=malloc(multicomp_freqs_max*sizeof(*ret->peak));
  for(i=0;i<multicomp_freqs_max;i++)
    ret->peak[i]=malloc(input_ch*sizeof(**ret->peak));
   
  ret->rms=malloc(multicomp_freqs_max*sizeof(*ret->rms));
  for(i=0;i<multicomp_freqs_max;i++)
    ret->rms[i]=malloc(input_ch*sizeof(**ret->rms));
  
  return (feedback_generic *)ret;
}

/* total, peak, rms are pulled in array[freqs][input_ch] order */

int pull_multicompand_feedback(float **peak,float **rms,int *b){
  multicompand_feedback *f=(multicompand_feedback *)feedback_pull(&ms.feedpool);
  int i;
  
  if(!f)return 0;
  
  if(f->bypass){
    feedback_old(&ms.feedpool,(feedback_generic *)f);
    return 2;
  }else{
    if(peak)
      for(i=0;i<f->freq_bands;i++)
	memcpy(peak[i],f->peak[i],sizeof(**peak)*input_ch);
    if(rms)
      for(i=0;i<f->freq_bands;i++)
	memcpy(rms[i],f->rms[i],sizeof(**rms)*input_ch);
    if(b)*b=f->freq_bands;
    feedback_old(&ms.feedpool,(feedback_generic *)f);
    return 1;
  }
}

static void reset_filters(multicompand_state *ms){
  int i,j;
  for(i=0;i<multicomp_freqs_max;i++)
    for(j=0;j<input_ch;j++){
      memset(&ms->over_peak[i][j],0,sizeof(peak_state));
      memset(&ms->under_peak[i][j],0,sizeof(peak_state));
      memset(&ms->base_peak[i][j],0,sizeof(peak_state));
      memset(&ms->over_iir[i][j],0,sizeof(iir_state));
      memset(&ms->under_iir[i][j],0,sizeof(iir_state));
      memset(&ms->base_iir[i][j],0,sizeof(iir_state));
    }
}

void multicompand_reset(){
  
  subband_reset(&ms.ss);
  while(pull_multicompand_feedback(NULL,NULL,NULL));
  reset_filters(&ms);

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

  ms.peak=calloc(multicomp_freqs_max,sizeof(*ms.peak));
  ms.rms=calloc(multicomp_freqs_max,sizeof(*ms.rms));
  for(i=0;i<multicomp_freqs_max;i++)ms.peak[i]=malloc(input_ch*sizeof(**ms.peak));
  for(i=0;i<multicomp_freqs_max;i++)ms.rms[i]=malloc(input_ch*sizeof(**ms.rms));
  
  return 0;
}

static int multicompand_filter_set(float msec,
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

  return 0;
}

int multicompand_over_attack_set(float msec){
  return multicompand_filter_set(msec,&ms.over_attack,1);
}

int multicompand_over_decay_set(float msec){
  return multicompand_filter_set(msec,&ms.over_decay,0);  
}

int multicompand_under_attack_set(float msec){
  return multicompand_filter_set(msec,&ms.under_attack,1);
}

int multicompand_under_decay_set(float msec){
  return multicompand_filter_set(msec,&ms.under_decay,0);
}

int multicompand_base_attack_set(float msec){
  return multicompand_filter_set(msec,&ms.base_attack,1);
}

int multicompand_base_decay_set(float msec){
  return multicompand_filter_set(msec,&ms.base_decay,0);
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
      if((x[ii]*x[ii])>val){
	val=(x[ii]*x[ii]);
	loc=ii+hold;
      }
  }
  
  if(val>peak[0])peak[0]=val;
  
  for(ii=1;ii<n;ii++){
    if((x[ii+ahead]*x[ii+ahead])>val){
      val=(x[ii+ahead]*x[ii+ahead]);
      loc=ii+ahead+hold;
    }	  
    if(ii>=loc){
      /* backfill */
      val=0;
      for(jj=ii+ahead-1;jj>=ii;jj--){
	if((x[jj]*x[jj])>val)val=(x[jj]*x[jj]);
	if(jj<n && val>peak[jj])peak[jj]=val;
      }
      val=(x[ii+ahead-1]*x[ii+ahead-1]);
      loc=ii+ahead+hold;
    }
    if(val>peak[ii])peak[ii]=val; 
  }

  ps->loc=loc-input_size;
  ps->val=val;
  
}

static void run_filter(float *dB,float *x,int n,
		       float lookahead,int mode,
		       iir_state *iir,iir_filter *attack,iir_filter *decay,
		       peak_state *ps){
  int i;
  memset(dB,0,sizeof(*dB)*n);
  
  if(mode)
    prepare_peak(dB, x, n,
		 step_ahead(attack->alpha)*lookahead, 
		 step_ahead(attack->alpha)*(1.-lookahead),
		 ps);
  else
    prepare_rms(dB, x, n, impulse_ahead2(attack->alpha)*lookahead);

  compute_iir2(dB, n, iir, attack, decay);
  
  for(i=0;i<n;i++)
    dB[i]=todB_a(dB+i)*.5f;

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
			 float *adj){
  int k;
  float overdB[input_size];
  float lookahead=c.over_lookahead/1000.;
  int mode=c.over_mode;

  run_filter(overdB,lx,input_size,lookahead,mode,iir,attack,decay,ps);
  
  if(adj){
    float corner_multiplier=(1.-1./(c.over_ratio*.001));
    if(c.over_softknee){
      for(k=0;k<input_size;k++)
	adj[k]+=soft_knee(overdB[k]-zerocorner)*corner_multiplier;
    }else{
      for(k=0;k<input_size;k++)
	adj[k]+=hard_knee(overdB[k]-zerocorner)*corner_multiplier;
    }
  }
}

static void base_compand(float *x,
			 iir_filter *attack, iir_filter *decay,
			 iir_state *iir, peak_state *ps,
			 float *adj){
  int k;
  float basedB[input_size];
  int mode=c.base_mode;

  run_filter(basedB,x,input_size,1.,mode,
	     iir,attack,decay,ps);
  
  if(adj){
    float base_multiplier=(1.-1./(c.base_ratio*.001));
    for(k=0;k<input_size;k++)
      adj[k]-=(basedB[k]+adj[k])*base_multiplier;
  }
}

static void under_compand(float *x,float zerocorner,
			 iir_filter *attack, iir_filter *decay,
			 iir_state *iir, peak_state *ps,
			 float *adj){
  int k;
  float underdB[input_size];
  float lookahead=c.under_lookahead/1000.;
  int mode=c.under_mode;

  run_filter(underdB,x,input_size,lookahead,mode,
	     iir,attack,decay,ps);
  
  if(adj){
    float corner_multiplier=(1./(c.under_ratio*.001)-1.);
    if(c.under_softknee){
      for(k=0;k<input_size;k++)
	adj[k]=soft_knee(zerocorner-underdB[k])*corner_multiplier;
    }else{
      for(k=0;k<input_size;k++)
	adj[k]=hard_knee(zerocorner-underdB[k])*corner_multiplier;
    }
  }
}

static void multicompand_work(void *vs){

  multicompand_state *ms=(multicompand_state *)vs;
  subband_state *ss=&ms->ss;
  int i,j,k,bypass_visible=1;
  float adj[input_size];

  float **peakfeed=ms->peak;
  float **rmsfeed=ms->rms;

  int bank;
  subband_window *w=ss->w1;
  subband_window *wP=ss->wP;
  int maxbands=ss->wC->freq_bands;
  if(maxbands<ss->w0->freq_bands)maxbands=ss->w0->freq_bands;
  if(maxbands<ss->w1->freq_bands)maxbands=ss->w1->freq_bands;

  if(w==&ms->sw[0]){
    bank=0;
  }else if(w==&ms->sw[1]){
    bank=1;
  }else bank=2;
  
  for(i=0;i<maxbands;i++){
    for(j=0;j<input_ch;j++){
      float *x=ss->lap[i][j];
      int active=(ss->effect_active1[j] || 
		  ss->effect_active0[j] || 
		  ss->effect_activeC[j]);

      if(active){
	/* one thing is worth a note here; 'maxbands' can be
	   'overrange' for the current bank.  This is intentional; we
	   may need to run the additional (allocated and valid)
	   filters before or after their bands are active.  The only
	   garbage data here is the xxxx_u, xxxx_o and xxxx_b
	   settings.  There are allocated, but unset; if overrange,
	   they're ignored in the compand worker */
	under_compand(x,  
		      bc[bank].static_u[i], 
		      &ms->under_attack,
		      &ms->under_decay,
		      &ms->under_iir[i][j],
		      &ms->under_peak[i][j],
		      (i>=w->freq_bands?0:adj));
	
	over_compand(x,  
		     bc[bank].static_o[i],
		     &ms->over_attack,
		     &ms->over_decay,
		     &ms->over_iir[i][j],
		     &ms->over_peak[i][j],
		     (i>=w->freq_bands?0:adj));

      }

      if(ss->visible1[j] && !mute_channel_muted(ss->mutemask1,j)){
	/* determine rms and peak for feedback */
	float max=-1.;
	int maxpos=-1;
	float rms=0.;
	if(bypass_visible){
	  int ii;
	  for(ii=0;ii<w->freq_bands;ii++){
	    memset(peakfeed[ii],0,input_ch*sizeof(**peakfeed));
	    memset(rmsfeed[ii],0,input_ch*sizeof(**rmsfeed));
	  }
	}
	bypass_visible=0;

	for(k=0;k<input_size;k++){
	  float val=x[k]*x[k];
	  if(val>max){
	    max=val;
	    maxpos=k;
	  }
	  rms+=val;
	}
	if(active){
	  peakfeed[i][j]=todB(max)*.5+adj[maxpos];
	  rmsfeed[i][j]=todB(rms/input_size)*.5+adj[maxpos];
	}else{
	  peakfeed[i][j]=todB(max)*.5;
	  rmsfeed[i][j]=todB(rms/input_size)*.5;
	}
      }
      
      if(active){
	base_compand(x,  
		     &ms->base_attack,
		     &ms->base_decay,
		     &ms->base_iir[i][j],
		     &ms->base_peak[i][j],
		     (i>=w->freq_bands?0:adj));
	
	if(ss->effect_activeC[j]){
	  for(k=0;k<input_size;k++)
	    x[k]*=fromdB_a(adj[k]);
	}
      } else if (ss->effect_activeP[j]){ 
	/* this lapping channel just became inactive */
	memset(&ms->over_peak[i][j],0,sizeof(peak_state));
	memset(&ms->under_peak[i][j],0,sizeof(peak_state));
	memset(&ms->base_peak[i][j],0,sizeof(peak_state));
	memset(&ms->over_iir[i][j],0,sizeof(iir_state));
	memset(&ms->under_iir[i][j],0,sizeof(iir_state));
	memset(&ms->base_iir[i][j],0,sizeof(iir_state));
      }

    }
  }

  for(;i<wP->freq_bands;i++)
    for(j=0;j<input_ch;j++){
      memset(&ms->over_peak[i][j],0,sizeof(peak_state));
      memset(&ms->under_peak[i][j],0,sizeof(peak_state));
      memset(&ms->base_peak[i][j],0,sizeof(peak_state));
      memset(&ms->over_iir[i][j],0,sizeof(iir_state));
      memset(&ms->under_iir[i][j],0,sizeof(iir_state));
      memset(&ms->base_iir[i][j],0,sizeof(iir_state));
    }

  /* finish up the state feedabck */
  if(bypass_visible){
    multicompand_feedback *ff=
      (multicompand_feedback *)
      feedback_new(&ms->feedpool,new_multicompand_feedback);
    ff->bypass=1;
    feedback_push(&ms->feedpool,(feedback_generic *)ff);
  }else{
    multicompand_feedback *ff=
      (multicompand_feedback *)
      feedback_new(&ms->feedpool,new_multicompand_feedback);
    
    for(i=0;i<w->freq_bands;i++){
      memcpy(ff->peak[i],ms->peak[i],input_ch*sizeof(**ms->peak));
      memcpy(ff->rms[i],ms->rms[i],input_ch*sizeof(**ms->rms));
    } 
    ff->bypass=0;
    ff->freq_bands=w->freq_bands;
    feedback_push(&ms->feedpool,(feedback_generic *)ff);
  }
}

time_linkage *multicompand_read(time_linkage *in){
  int visible[input_ch];
  int active[input_ch];
  int i;

  for(i=0;i<input_ch;i++){
    visible[i]=compand_visible;
    active[i]=compand_active;
  }
  return subband_read(in, &ms.ss, &ms.sw[c.active_bank],
		      visible,active,multicompand_work,&ms);

}


