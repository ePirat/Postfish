/*
 *
 *  postfish
 *    
 *      Copyright (C) 2002-2005 Monty and Xiph.Org
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
#include "follower.h"

extern off_t offset;
extern int offch;

/* feedback! */
typedef struct multicompand_feedback{
  feedback_generic parent_class;
  float **peak;
  float **rms;
  int freq_bands;
  int bypass;
} multicompand_feedback;

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
  
  iir_filter over_attack[MAX_INPUT_CHANNELS];
  iir_filter over_decay[MAX_INPUT_CHANNELS];

  iir_filter under_attack[MAX_INPUT_CHANNELS];
  iir_filter under_decay[MAX_INPUT_CHANNELS];

  iir_filter base_attack[MAX_INPUT_CHANNELS];
  iir_filter base_decay[MAX_INPUT_CHANNELS];

  iir_state over_iir[multicomp_freqs_max][MAX_INPUT_CHANNELS];
  int over_delay[MAX_INPUT_CHANNELS];

  iir_state under_iir[multicomp_freqs_max][MAX_INPUT_CHANNELS];
  int under_delay[MAX_INPUT_CHANNELS];

  iir_state base_iir[multicomp_freqs_max][MAX_INPUT_CHANNELS];
  int base_delay[MAX_INPUT_CHANNELS];

  peak_state over_peak[multicomp_freqs_max][MAX_INPUT_CHANNELS];
  peak_state under_peak[multicomp_freqs_max][MAX_INPUT_CHANNELS];
  peak_state base_peak[multicomp_freqs_max][MAX_INPUT_CHANNELS];

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

static void reset_filters_onech(multicompand_state *ms,int ch){
  int i;
  /* delays are only used to softstart individual filters that went
     inactive at unity; the key is that we know they're starting from
     mult of zero, which is not necessarily true at a reset */
  ms->base_delay[ch]=0;
  ms->over_delay[ch]=0;
  ms->under_delay[ch]=0;
  for(i=0;i<multicomp_freqs_max;i++){
    memset(&ms->over_peak[i][ch],0,sizeof(peak_state));
    memset(&ms->under_peak[i][ch],0,sizeof(peak_state));
    memset(&ms->base_peak[i][ch],0,sizeof(peak_state));
    memset(&ms->over_iir[i][ch],0,sizeof(iir_state));
    memset(&ms->under_iir[i][ch],0,sizeof(iir_state));
    memset(&ms->base_iir[i][ch],0,sizeof(iir_state));
  }
}

static void reset_filters(multicompand_state *ms){
  int j;
  for(j=0;j<ms->ch;j++)
    reset_filters_onech(ms,j);
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

  ms->peak=calloc(multicomp_freqs_max,sizeof(*ms->peak));
  ms->rms=calloc(multicomp_freqs_max,sizeof(*ms->rms));
  for(i=0;i<multicomp_freqs_max;i++)ms->peak[i]=malloc(ms->ch*sizeof(**ms->peak));
  for(i=0;i<multicomp_freqs_max;i++)ms->rms[i]=malloc(ms->ch*sizeof(**ms->rms));

  ms->prevset=malloc(ch*sizeof(*ms->prevset));
  ms->currset=malloc(ch*sizeof(*ms->currset));

  reset_filters(ms);
  
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

static void filter_set1(multicompand_state *ms,
		       float msec,
		       iir_filter *filter){
  float alpha;
  float corner_freq= 500./msec;

  alpha=corner_freq/input_rate;
  filter->g=mkbessel(alpha,1,filter->c);
  filter->alpha=alpha;
  filter->Hz=alpha*input_rate;

}

static void filter_set2(multicompand_state *ms,
		       float msec,
		       iir_filter *filter){
  float alpha;
  float corner_freq= 500./msec;

  /* make sure the chosen frequency doesn't require a lookahead
     greater than what's available */
  if(step_freq(input_size*2-ms->ss.qblocksize*3)*1.01>corner_freq)
    corner_freq=step_freq(input_size*2-ms->ss.qblocksize*3);
  
  alpha=corner_freq/input_rate;
  filter->g=mkbessel(alpha,2,filter->c);
  filter->alpha=alpha;
  filter->Hz=alpha*input_rate;

}

static void filterbank_set1(multicompand_state *ms,
			   float msec,
			   iir_filter *filter){
  int i;
  for(i=0;i<ms->ch;i++)
    filter_set1(ms,msec,filter+i);

}

static void filterbank_set2(multicompand_state *ms,
			   float msec,
			   iir_filter *filter){
  int i;
  for(i=0;i<ms->ch;i++)
    filter_set2(ms,msec,filter+i);

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

  int o_active=0,u_active=0,b_active=0;

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

    /* don't run filters that will be applied at unity */
    if(prevset->under_ratio==1000 && currset->under_ratio==1000){
      ms->under_delay[channel]=2;
      for(i=0;i<multicomp_freqs_max;i++){
	memset(&ms->under_peak[i][channel],0,sizeof(peak_state));
	memset(&ms->under_iir[i][channel],0,sizeof(iir_state));
      }
    }else{
      if(ms->under_delay[channel]-->0)currset->under_ratio=1000;
      if(ms->under_delay[channel]<0)ms->under_delay[channel]=0;
      u_active=1;
    }

    if(prevset->over_ratio==1000 && currset->over_ratio==1000){
      ms->over_delay[channel]=2;
      for(i=0;i<multicomp_freqs_max;i++){
	memset(&ms->over_peak[i][channel],0,sizeof(peak_state));
	memset(&ms->over_iir[i][channel],0,sizeof(iir_state));
      }
    }else{
      if(ms->over_delay[channel]-->0)currset->over_ratio=1000;
      if(ms->over_delay[channel]<0)ms->over_delay[channel]=0;
      o_active=1;
    }

    if(prevset->base_ratio==1000 && currset->base_ratio==1000){
      ms->base_delay[channel]=2;
      for(i=0;i<multicomp_freqs_max;i++){
	memset(&ms->base_peak[i][channel],0,sizeof(peak_state));
	memset(&ms->base_iir[i][channel],0,sizeof(iir_state));
      }
    }else{
      if(ms->base_delay[channel]-->0)currset->base_ratio=1000;
      if(ms->base_delay[channel]<0)ms->base_delay[channel]=0;
      b_active=1;
    }

  } else if (ss->effect_activeP[channel]){ 
    /* this lapping channel just became inactive */
    reset_filters_onech(ms,channel);
  }   
  
  /* one thing is worth a note here; 'maxbands' can be
     'overrange' for the current bank.  This is intentional; we
     may need to run the additional (allocated and valid)
     filters before or after their bands are active.  The only
     garbage data here is the xxxx_u, xxxx_o and xxxx_b
     settings.  There are allocated, but unset; if overrange,
     they're ignored in the compand worker */

  for(i=0;i<maxbands;i++){
    
    float *x=ss->lap[i][channel];
    
    if(u_active || o_active || b_active)
      memset(adj,0,sizeof(*adj)*input_size);
      
    if(u_active)
      bi_compand(x,0,(i>=w->freq_bands?0:adj),
		 //prevset->static_u[i],
		 currset->static_u[i],
		 1.f-1000.f/prevset->under_ratio,
		 1.f-1000.f/currset->under_ratio,
		 c->under_lookahead/1000.f,
		 c->under_mode,
		 c->under_softknee,
		 &ms->under_attack[channel],
		 &ms->under_decay[channel],
		 &ms->under_iir[i][channel],
		 &ms->under_peak[i][channel],
		 ss->effect_active1[channel] && 
		 (i<w->freq_bands),
		 0);

    if(o_active)
      bi_compand(x,0,(i>=w->freq_bands?0:adj),
		 //prevset->static_o[i],
		 currset->static_o[i],
		 1.f-1000.f/prevset->over_ratio,
		 1.f-1000.f/currset->over_ratio,
		 c->over_lookahead/1000.f,
		 c->over_mode,
		 c->over_softknee,
		 &ms->over_attack[channel],
		 &ms->over_decay[channel],
		 &ms->over_iir[i][channel],
		 &ms->over_peak[i][channel],
		 ss->effect_active1[channel] && 
		 (i<w->freq_bands),
		 1);

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
	if(u_active || o_active || b_active){
	  peakfeed[i][channel]=todB(max)*.5+adj[maxpos];
	  rmsfeed[i][channel]=todB(rms/input_size)*.5+adj[maxpos];
	}else{
	  peakfeed[i][channel]=todB(max)*.5;
	  rmsfeed[i][channel]=todB(rms/input_size)*.5;
	}
      }
    }
    
    if(b_active)
      full_compand(x,0,(i>=w->freq_bands?0:adj),
		   1.f-1000.f/prevset->base_ratio,
		   1.f-1000.f/currset->base_ratio,
		   c->base_mode,
		   &ms->base_attack[channel],
		   &ms->base_decay[channel],
		   &ms->base_iir[i][channel],
		   &ms->base_peak[i][channel],
		   ss->effect_active1[channel] &&
		   i<w->freq_bands);

    if(u_active || o_active || b_active){
      if(ss->effect_active1[channel]){
	for(k=0;k<input_size;k++)
	  x[k]*=fromdB_a(adj[k]);
      }
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
      filterbank_set2(ms,o_attackms,ms->over_attack);
    if(o_decayms !=ms->over_decay[0].ms)  
      filterbank_set1(ms,o_decayms,ms->over_decay);
    if(u_attackms!=ms->under_attack[0].ms)
      filterbank_set2(ms,u_attackms,ms->under_attack);
    if(u_decayms !=ms->under_decay[0].ms) 
      filterbank_set1(ms,u_decayms,ms->under_decay);
    if(b_attackms!=ms->base_attack[0].ms) 
      filterbank_set2(ms,b_attackms,ms->base_attack);
    if(b_decayms !=ms->base_decay[0].ms)  
      filterbank_set1(ms,b_decayms,ms->base_decay);
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
      filter_set2(ms,o_attackms,ms->over_attack+i);
    if(o_decayms !=ms->over_decay[i].ms)  
      filter_set1(ms,o_decayms,ms->over_decay+i);
    if(u_attackms!=ms->under_attack[i].ms)
      filter_set2(ms,u_attackms,ms->under_attack+i);
    if(u_decayms !=ms->under_decay[i].ms) 
      filter_set1(ms,u_decayms,ms->under_decay+i);
    if(b_attackms!=ms->base_attack[i].ms) 
      filter_set2(ms,b_attackms,ms->base_attack+i);
    if(b_decayms !=ms->base_decay[i].ms)  
      filter_set1(ms,b_decayms,ms->base_decay+i);

    w[i]=&sw[multi_channel_set[i].active_bank];
    visible[i]=multi_channel_set[i].panel_visible;
    active[i]=multi_channel_set[i].panel_active;
  }
  
  return subband_read(in, &channel_state.ss, w, visible, active,
		      multicompand_work_channel,&channel_state);
}


