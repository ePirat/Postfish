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
  sig_atomic_t static_u[multicomp_freqs_max];
  sig_atomic_t under_ratio;

  sig_atomic_t static_o[multicomp_freqs_max];
  sig_atomic_t over_ratio;
  
  sig_atomic_t base_ratio;
} atten_cache;

typedef struct {
  feedback_generic_pool feedpool;
  subband_state ss;
  
  iir_filter *over_attack;
  iir_filter *over_decay;

  iir_filter *under_attack;
  iir_filter *under_decay;

  iir_filter *base_attack;
  iir_filter *base_decay;

  iir_state *over_iir[multicomp_freqs_max];
  iir_state *under_iir[multicomp_freqs_max];
  iir_state *base_iir[multicomp_freqs_max];

  peak_state *over_peak[multicomp_freqs_max];
  peak_state *under_peak[multicomp_freqs_max];
  peak_state *base_peak[multicomp_freqs_max];

  atten_cache *prevset;
  atten_cache *currset;
  
  float **peak;
  float **rms;
  int ch;
  int initstate;
} multicompand_state;

multicompand_settings multi_master_set;
multicompand_settings *multi_channel_set;

static multicompand_state master_state;
static multicompand_state channel_state;

static subband_window sw[multicomp_banks];

static feedback_generic *new_multicompand_feedback(void){
  multicompand_feedback *ret=calloc(1,sizeof(*ret));
  return (feedback_generic *)ret;
}

/* total, peak, rms are pulled in array[freqs][input_ch] order */

static int pull_multicompand_feedback(multicompand_state *ms,float **peak,float **rms,int *b){
  multicompand_feedback *f=(multicompand_feedback *)feedback_pull(&ms->feedpool);
  int i;
  
  if(!f)return 0;
  
  if(f->bypass){
    feedback_old(&ms->feedpool,(feedback_generic *)f);
    return 2;
  }else{
    if(peak)
      for(i=0;i<f->freq_bands;i++)
	memcpy(peak[i],f->peak[i],sizeof(**peak)*ms->ch);
    if(rms)
      for(i=0;i<f->freq_bands;i++)
	memcpy(rms[i],f->rms[i],sizeof(**rms)*ms->ch);
    if(b)*b=f->freq_bands;
    feedback_old(&ms->feedpool,(feedback_generic *)f);
    return 1;
  }
}

int pull_multicompand_feedback_master(float **peak,float **rms,int *b){
  return pull_multicompand_feedback(&master_state,peak,rms,b);
}

int pull_multicompand_feedback_channel(float **peak,float **rms,int *b){
  return pull_multicompand_feedback(&channel_state,peak,rms,b);
}

static void reset_filters(multicompand_state *ms){
  int i,j;
  for(i=0;i<multicomp_freqs_max;i++)
    for(j=0;j<ms->ch;j++){
      memset(&ms->over_peak[i][j],0,sizeof(peak_state));
      memset(&ms->under_peak[i][j],0,sizeof(peak_state));
      memset(&ms->base_peak[i][j],0,sizeof(peak_state));
      memset(&ms->over_iir[i][j],0,sizeof(iir_state));
      memset(&ms->under_iir[i][j],0,sizeof(iir_state));
      memset(&ms->base_iir[i][j],0,sizeof(iir_state));
    }
}

void multicompand_reset(){
  
  subband_reset(&master_state.ss);
  subband_reset(&channel_state.ss);
  while(pull_multicompand_feedback_master(NULL,NULL,NULL));
  while(pull_multicompand_feedback_channel(NULL,NULL,NULL));
  reset_filters(&master_state);
  reset_filters(&channel_state);

  master_state.initstate=0;
  channel_state.initstate=0;
}

static int multicompand_load_helper(multicompand_state *ms,int ch){
  int i;
  int qblocksize=input_size/8;
  memset(ms,0,sizeof(*ms));
  
  ms->ch=ch;
  subband_load(&ms->ss,multicomp_freqs_max,qblocksize,ch);

  ms->over_attack=calloc(ms->ch,sizeof(*ms->over_attack));
  ms->over_decay=calloc(ms->ch,sizeof(*ms->over_decay));

  ms->under_attack=calloc(ms->ch,sizeof(*ms->under_attack));
  ms->under_decay=calloc(ms->ch,sizeof(*ms->under_decay));

  ms->base_attack=calloc(ms->ch,sizeof(*ms->base_attack));
  ms->base_decay=calloc(ms->ch,sizeof(*ms->base_decay));

  for(i=0;i<multicomp_freqs_max;i++){
    ms->over_peak[i]=calloc(ms->ch,sizeof(peak_state));
    ms->under_peak[i]=calloc(ms->ch,sizeof(peak_state));
    ms->base_peak[i]=calloc(ms->ch,sizeof(peak_state));
    ms->over_iir[i]=calloc(ms->ch,sizeof(iir_state));
    ms->under_iir[i]=calloc(ms->ch,sizeof(iir_state));
    ms->base_iir[i]=calloc(ms->ch,sizeof(iir_state));
  }

  ms->peak=calloc(multicomp_freqs_max,sizeof(*ms->peak));
  ms->rms=calloc(multicomp_freqs_max,sizeof(*ms->rms));
  for(i=0;i<multicomp_freqs_max;i++)ms->peak[i]=malloc(ms->ch*sizeof(**ms->peak));
  for(i=0;i<multicomp_freqs_max;i++)ms->rms[i]=malloc(ms->ch*sizeof(**ms->rms));

  ms->prevset=malloc(ch*sizeof(*ms->prevset));
  ms->currset=malloc(ch*sizeof(*ms->currset));
  
  return 0;
}

int multicompand_load(int outch){
  int i;
  multi_channel_set=calloc(input_ch,sizeof(*multi_channel_set));
  multicompand_load_helper(&master_state,outch);
  multicompand_load_helper(&channel_state,input_ch);

  for(i=0;i<multicomp_banks;i++)
    subband_load_freqs(&master_state.ss,&sw[i],multicomp_freq_list[i],
		       multicomp_freqs[i]);
    
  return 0;
}

static void filter_set(multicompand_state *ms,
		       float msec,
		       iir_filter *filter,
		       int attackp){
  float alpha;
  float corner_freq= 500./msec;

  /* make sure the chosen frequency doesn't require a lookahead
     greater than what's available */
  if(step_freq(input_size*2-ms->ss.qblocksize*3)*1.01>corner_freq && attackp)
    corner_freq=step_freq(input_size*2-ms->ss.qblocksize*3);

  alpha=corner_freq/input_rate;
  filter->g=mkbessel(alpha,2,filter->c);
  filter->alpha=alpha;
  filter->Hz=alpha*input_rate;

}

static void filterbank_set(multicompand_state *ms,
			   float msec,
			   iir_filter *filter,
			   int attackp){
  int i;
  for(i=0;i<ms->ch;i++)
    filter_set(ms,msec,filter+i,attackp);

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

static void over_compand(float *lx,
			 float zerocorner,
			 float currcorner,
			 iir_filter *attack, iir_filter *decay,
			 iir_state *iir, peak_state *ps,
			 float lookahead,int mode,int knee,
			 float prevratio,
			 float currratio,
			 float *adj){
  int k;
  float overdB[input_size];

  run_filter(overdB,lx,input_size,lookahead,mode,iir,attack,decay,ps);
  
  if(adj){
    float ratio_multiplier= 1.- 1000./prevratio;
    
    if(zerocorner!=currcorner || prevratio!=currratio){
      /* slew limit these attenuators */
      float ratio_add= ((1.- 1000./currratio)-ratio_multiplier)/input_size;
      float corner_add= (currcorner-zerocorner)/input_size;
      
      if(knee){
	for(k=0;k<input_size;k++){
	  adj[k]+=soft_knee(overdB[k]-zerocorner)*ratio_multiplier;
	  ratio_multiplier+=ratio_add;
	  zerocorner+=corner_add;
	}
      }else{
	for(k=0;k<input_size;k++){
	  adj[k]+=hard_knee(overdB[k]-zerocorner)*ratio_multiplier;
	  ratio_multiplier+=ratio_add;
	  zerocorner+=corner_add;
	}
      }
    }else{
      if(knee){
	for(k=0;k<input_size;k++)
	  adj[k]+=soft_knee(overdB[k]-zerocorner)*ratio_multiplier;
      }else{
	for(k=0;k<input_size;k++)
	  adj[k]+=hard_knee(overdB[k]-zerocorner)*ratio_multiplier;
      }
    }
  }
}

static void base_compand(float *x,
			 iir_filter *attack, iir_filter *decay,
			 iir_state *iir, peak_state *ps,
			 int mode,
			 float prevratio,
			 float currratio,
			 float *adj){
  int k;
  float basedB[input_size];

  run_filter(basedB,x,input_size,1.,mode,
	     iir,attack,decay,ps);
  
  if(adj){
    float ratio_multiplier=1.-1000./prevratio;

    if(prevratio!=currratio){
      /* slew limit the attenuators */
      float ratio_add= ((1.- 1000./currratio)-ratio_multiplier)/input_size;

      for(k=0;k<input_size;k++){
	adj[k]-=(basedB[k]+adj[k])*ratio_multiplier;
	ratio_multiplier+=ratio_add;
      }

    }else{
      for(k=0;k<input_size;k++)
	adj[k]-=(basedB[k]+adj[k])*ratio_multiplier;
    }
  }
}

static void under_compand(float *x,
			 float zerocorner,
			 float currcorner,
			 iir_filter *attack, iir_filter *decay,
			 iir_state *iir, peak_state *ps,
			 float lookahead,int mode,int knee,
			 float prevratio,
			 float currratio,
			 float *adj){
  int k;
  float underdB[input_size];

  run_filter(underdB,x,input_size,lookahead,mode,
	     iir,attack,decay,ps);
  
  if(adj){
    float ratio_multiplier=1000./prevratio - 1.;
    
    if(zerocorner!=currcorner || prevratio!=currratio){
      /* slew limit these attenuators */
      float ratio_add= ((1000./currratio - 1.)-ratio_multiplier)/input_size;
      float corner_add= (currcorner-zerocorner)/input_size;
      
      if(knee){
	for(k=0;k<input_size;k++){
	  adj[k]=soft_knee(zerocorner-underdB[k])*ratio_multiplier;
	  ratio_multiplier+=ratio_add;
	  zerocorner+=corner_add;
	}
      }else{
	for(k=0;k<input_size;k++){
	  adj[k]=hard_knee(zerocorner-underdB[k])*ratio_multiplier;
	  ratio_multiplier+=ratio_add;
	  zerocorner+=corner_add;
	}
      }

    }else{
      if(knee){
	for(k=0;k<input_size;k++)
	  adj[k]=soft_knee(zerocorner-underdB[k])*ratio_multiplier;
      }else{
	for(k=0;k<input_size;k++)
	  adj[k]=hard_knee(zerocorner-underdB[k])*ratio_multiplier;
      }
    }
  }
}

static int find_maxbands(subband_state *ss,int channel){
  int maxbands=ss->wC[channel]->freq_bands;
  if(maxbands<ss->w0[channel]->freq_bands)maxbands=ss->w0[channel]->freq_bands;
  if(maxbands<ss->w1[channel]->freq_bands)maxbands=ss->w1[channel]->freq_bands;
  return maxbands;
}

static int multicompand_work_perchannel(multicompand_state *ms, 
					float **peakfeed,
					float **rmsfeed,
					int maxbands,
					int channel,
					atten_cache *prevset,
					atten_cache *currset,
					multicompand_settings *c){
  subband_state *ss=&ms->ss;
  int active=(ss->effect_active1[channel] || 
	      ss->effect_active0[channel] || 
	      ss->effect_activeC[channel]);
    
  int feedback_p=0;
  int i,k,bank;
  subband_window *w=ss->w1[channel];
  subband_window *wP=ss->wP[channel];
  float adj[input_size];
  

  if(w==&sw[0]){
    bank=0;
  }else if(w==&sw[1]){
    bank=1;
  }else bank=2;

  if(active){
    for(i=0;i<multicomp_freqs_max;i++){
      currset->static_u[i]=c->bc[bank].static_u[i];
      currset->static_o[i]=c->bc[bank].static_o[i];
    }
    
    currset->under_ratio=c->under_ratio;
    currset->over_ratio=c->over_ratio;
    currset->base_ratio=c->base_ratio;
    
    /* don't slew from an unknown value */
    if(!ss->effect_activeP[channel] || !ms->initstate) 
      memcpy(prevset,currset,sizeof(*currset));
  }

  for(i=0;i<maxbands;i++){
    
    float *x=ss->lap[i][channel];
    
    if(active){
      /* one thing is worth a note here; 'maxbands' can be
	 'overrange' for the current bank.  This is intentional; we
	 may need to run the additional (allocated and valid)
	 filters before or after their bands are active.  The only
	 garbage data here is the xxxx_u, xxxx_o and xxxx_b
	 settings.  There are allocated, but unset; if overrange,
	 they're ignored in the compand worker */
      under_compand(x,  
		    prevset->static_u[i],
		    currset->static_u[i],
		    &ms->under_attack[channel],
		    &ms->under_decay[channel],
		    &ms->under_iir[i][channel],
		    &ms->under_peak[i][channel],
		    c->under_lookahead/1000.,
		    c->under_mode,
		    c->under_softknee,
		    prevset->under_ratio,
		    currset->under_ratio,
		    (i>=w->freq_bands?0:adj));
      
      over_compand(x,  
		   prevset->static_o[i],
		   currset->static_o[i],
		   &ms->over_attack[channel],
		   &ms->over_decay[channel],
		   &ms->over_iir[i][channel],
		   &ms->over_peak[i][channel],
		   c->over_lookahead/1000.,
		   c->over_mode,
		   c->over_softknee,
		   prevset->over_ratio,
		   currset->over_ratio,
		   (i>=w->freq_bands?0:adj));
      
    }
    
    if(ss->visible1[channel]){

      feedback_p=1;
	
      if(!mute_channel_muted(ss->mutemask1,channel)){
	/* determine rms and peak for feedback */
	float max=-1.;
	int maxpos=-1;
	float rms=0.;
	
	for(k=0;k<input_size;k++){
	  float val=x[k]*x[k];
	  if(val>max){
	    max=val;
	    maxpos=k;
	  }
	  rms+=val;
	}
	if(active){
	  peakfeed[i][channel]=todB(max)*.5+adj[maxpos];
	  rmsfeed[i][channel]=todB(rms/input_size)*.5+adj[maxpos];
	}else{
	  peakfeed[i][channel]=todB(max)*.5;
	  rmsfeed[i][channel]=todB(rms/input_size)*.5;
	}
      }
    }
    
    if(active){
      base_compand(x,  
		   &ms->base_attack[channel],
		   &ms->base_decay[channel],
		   &ms->base_iir[i][channel],
		   &ms->base_peak[i][channel],
		   c->base_mode,
		   prevset->base_ratio,
		   currset->base_ratio,
		   (i>=w->freq_bands?0:adj));
      
      if(ss->effect_active1[channel]){
	for(k=0;k<input_size;k++)
	  x[k]*=fromdB_a(adj[k]);
      }
    } else if (ss->effect_activeP[channel]){ 
      /* this lapping channel just became inactive */
      memset(&ms->over_peak[i][channel],0,sizeof(peak_state));
      memset(&ms->under_peak[i][channel],0,sizeof(peak_state));
      memset(&ms->base_peak[i][channel],0,sizeof(peak_state));
      memset(&ms->over_iir[i][channel],0,sizeof(iir_state));
      memset(&ms->under_iir[i][channel],0,sizeof(iir_state));
      memset(&ms->base_iir[i][channel],0,sizeof(iir_state));
    }   
  }
  
  for(;i<wP->freq_bands;i++){
    memset(&ms->over_peak[i][channel],0,sizeof(peak_state));
    memset(&ms->under_peak[i][channel],0,sizeof(peak_state));
    memset(&ms->base_peak[i][channel],0,sizeof(peak_state));
    memset(&ms->over_iir[i][channel],0,sizeof(iir_state));
    memset(&ms->under_iir[i][channel],0,sizeof(iir_state));
    memset(&ms->base_iir[i][channel],0,sizeof(iir_state));
  }
  return(feedback_p);
}

static void push_feedback(multicompand_state *ms,int bypass,int maxmaxbands){
  int i;

  if(bypass){
    multicompand_feedback *ff=
      (multicompand_feedback *)
      feedback_new(&ms->feedpool,new_multicompand_feedback);
    ff->bypass=1;
    feedback_push(&ms->feedpool,(feedback_generic *)ff);
  }else{
    multicompand_feedback *ff=
      (multicompand_feedback *)
      feedback_new(&ms->feedpool,new_multicompand_feedback);

    if(!ff->peak){
      ff->peak=malloc(multicomp_freqs_max*sizeof(*ff->peak));
      ff->rms=malloc(multicomp_freqs_max*sizeof(*ff->rms));
  
      for(i=0;i<multicomp_freqs_max;i++)
	ff->rms[i]=malloc(ms->ch*sizeof(**ff->rms));
      for(i=0;i<multicomp_freqs_max;i++)
	ff->peak[i]=malloc(ms->ch*sizeof(**ff->peak));
    }
    
    for(i=0;i<maxmaxbands;i++){
      memcpy(ff->peak[i],ms->peak[i],ms->ch*sizeof(**ms->peak));
      memcpy(ff->rms[i],ms->rms[i],ms->ch*sizeof(**ms->rms));
    } 
    ff->bypass=0;
    ff->freq_bands=maxmaxbands;
    feedback_push(&ms->feedpool,(feedback_generic *)ff);
  }
}

static void multicompand_work_master(void *vs){
  multicompand_state *ms=(multicompand_state *)vs;
  int i,j,bypass_visible=1;
  int maxmaxbands=0;

  for(i=0;i<multicomp_freqs_max;i++){
    for(j=0;j<ms->ch;j++){
      ms->peak[i][j]=-150.;
      ms->rms[i][j]=-150;
    }
  }
 
  for(i=0;i<ms->ch;i++){
    int maxbands=find_maxbands(&ms->ss,i);
    if(maxbands>maxmaxbands)maxmaxbands=maxbands;
    if(multicompand_work_perchannel(ms, ms->peak, ms->rms, maxbands, i, 
				    &ms->prevset[i], &ms->currset[i], &multi_master_set))
      bypass_visible=0;
  }
  {
    atten_cache *temp=ms->prevset;
    ms->prevset=ms->currset;
    ms->currset=temp;
    ms->initstate=1;
  }

  push_feedback(ms,bypass_visible,maxmaxbands);
}

static void multicompand_work_channel(void *vs){
  multicompand_state *ms=(multicompand_state *)vs;
  int i,j,bypass_visible=1;
  int maxmaxbands=0;

  for(i=0;i<multicomp_freqs_max;i++){
    for(j=0;j<ms->ch;j++){
      ms->peak[i][j]=-150.;
      ms->rms[i][j]=-150;
    }
  }

  for(i=0;i<ms->ch;i++){
    int maxbands=find_maxbands(&ms->ss,i);
    if(maxbands>maxmaxbands)maxmaxbands=maxbands;
    if(multicompand_work_perchannel(ms, ms->peak, ms->rms, maxbands, i, 
				    &ms->prevset[i], &ms->currset[i], multi_channel_set+i))
      bypass_visible=0;
  }
  {
    atten_cache *temp=ms->prevset;
    ms->prevset=ms->currset;
    ms->currset=temp;
    ms->initstate=1;
  }

  push_feedback(ms,bypass_visible,maxmaxbands);
}

time_linkage *multicompand_read_master(time_linkage *in){
  multicompand_state *ms=&master_state;
  int visible[ms->ch];
  int active[ms->ch];
  subband_window *w[ms->ch];
  int i,ab=multi_master_set.active_bank;
  
  for(i=0;i<ms->ch;i++){
    visible[i]=multi_master_set.panel_visible;
    active[i]=multi_master_set.panel_active;
    w[i]=&sw[ab];
  }

  /* do any filters need updated from UI changes? */
  {
    float o_attackms=multi_master_set.over_attack*.1;
    float o_decayms=multi_master_set.over_decay*.1;
    float u_attackms=multi_master_set.under_attack*.1;
    float u_decayms=multi_master_set.under_decay*.1;
    float b_attackms=multi_master_set.base_attack*.1;
    float b_decayms=multi_master_set.base_decay*.1;

    if(o_attackms!=ms->over_attack[0].ms) 
      filterbank_set(ms,o_attackms,ms->over_attack,1);
    if(o_decayms !=ms->over_decay[0].ms)  
      filterbank_set(ms,o_decayms,ms->over_decay,0);
    if(u_attackms!=ms->under_attack[0].ms)
      filterbank_set(ms,u_attackms,ms->under_attack,1);
    if(u_decayms !=ms->under_decay[0].ms) 
      filterbank_set(ms,u_decayms,ms->under_decay,0);
    if(b_attackms!=ms->base_attack[0].ms) 
      filterbank_set(ms,b_attackms,ms->base_attack,1);
    if(b_decayms !=ms->base_decay[0].ms)  
      filterbank_set(ms,b_decayms,ms->base_decay,0);
  }

  return subband_read(in, &master_state.ss, w, visible,active,
		      multicompand_work_master,&master_state);
}

time_linkage *multicompand_read_channel(time_linkage *in){
  multicompand_state *ms=&channel_state;
  int visible[ms->ch];
  int active[ms->ch];
  subband_window *w[ms->ch];
  int i;
  
  for(i=0;i<ms->ch;i++){

    /* do any filters need updated from UI changes? */
    float o_attackms=multi_channel_set[i].over_attack*.1;
    float o_decayms=multi_channel_set[i].over_decay*.1;
    float u_attackms=multi_channel_set[i].under_attack*.1;
    float u_decayms=multi_channel_set[i].under_decay*.1;
    float b_attackms=multi_channel_set[i].base_attack*.1;
    float b_decayms=multi_channel_set[i].base_decay*.1;

    if(o_attackms!=ms->over_attack[i].ms) 
      filter_set(ms,o_attackms,ms->over_attack+i,1);
    if(o_decayms !=ms->over_decay[i].ms)  
      filter_set(ms,o_decayms,ms->over_decay+i,0);
    if(u_attackms!=ms->under_attack[i].ms)
      filter_set(ms,u_attackms,ms->under_attack+i,1);
    if(u_decayms !=ms->under_decay[i].ms) 
      filter_set(ms,u_decayms,ms->under_decay+i,0);
    if(b_attackms!=ms->base_attack[i].ms) 
      filter_set(ms,b_attackms,ms->base_attack+i,1);
    if(b_decayms !=ms->base_decay[i].ms)  
      filter_set(ms,b_decayms,ms->base_decay+i,0);

    w[i]=&sw[multi_channel_set[i].active_bank];
    visible[i]=multi_channel_set[i].panel_visible;
    active[i]=multi_channel_set[i].panel_active;
  }
  
  return subband_read(in, &channel_state.ss, w, visible, active,
		      multicompand_work_channel,&channel_state);
}


