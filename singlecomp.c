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
#include "window.h"
#include "follower.h"
#include "singlecomp.h"

extern int input_size;
extern int input_rate;
extern int input_ch;

typedef struct {
  sig_atomic_t u_thresh;
  sig_atomic_t u_ratio;

  sig_atomic_t o_thresh;
  sig_atomic_t o_ratio;
  
  sig_atomic_t b_ratio;
} atten_cache;

typedef struct{
  time_linkage out;
  feedback_generic_pool feedpool;

  iir_state o_iir[MAX_INPUT_CHANNELS];
  iir_state u_iir[MAX_INPUT_CHANNELS];
  iir_state b_iir[MAX_INPUT_CHANNELS];
  int o_delay[MAX_INPUT_CHANNELS];
  int u_delay[MAX_INPUT_CHANNELS];
  int b_delay[MAX_INPUT_CHANNELS];

  peak_state o_peak[MAX_INPUT_CHANNELS];
  peak_state u_peak[MAX_INPUT_CHANNELS];
  peak_state b_peak[MAX_INPUT_CHANNELS];

  iir_filter o_attack[MAX_INPUT_CHANNELS];
  iir_filter u_attack[MAX_INPUT_CHANNELS];
  iir_filter b_attack[MAX_INPUT_CHANNELS];
  iir_filter o_decay[MAX_INPUT_CHANNELS];
  iir_filter u_decay[MAX_INPUT_CHANNELS];
  iir_filter b_decay[MAX_INPUT_CHANNELS];

  int fillstate;
  float **cache;
  int cache_samples;

  int *activeP;
  int *active0;

  int mutemaskP;
  int mutemask0;
  int ch;

  atten_cache *prevset;
  atten_cache *currset;
} singlecomp_state;

static float *window;

singlecomp_settings  singlecomp_master_set;
singlecomp_settings *singlecomp_channel_set;

static singlecomp_settings **master_set_bundle;
static singlecomp_settings **channel_set_bundle;

static singlecomp_state     master_state;
static singlecomp_state     channel_state;

/* feedback! */
typedef struct singlecomp_feedback{
  feedback_generic parent_class;
  float *peak;
  float *rms;
} singlecomp_feedback;

static feedback_generic *new_singlecomp_feedback(void){
  singlecomp_feedback *ret=calloc(1,sizeof(*ret));
  return (feedback_generic *)ret;
}

static int pull_singlecomp_feedback(singlecomp_state *scs, float *peak,float *rms){
  singlecomp_feedback *f=(singlecomp_feedback *)feedback_pull(&scs->feedpool);
  
  if(!f)return 0;
  
  if(peak)
    memcpy(peak,f->peak,sizeof(*peak)*scs->ch);
  if(rms)
    memcpy(rms,f->rms,sizeof(*rms)*scs->ch);
  feedback_old(&scs->feedpool,(feedback_generic *)f);
  return 1;
}

int pull_singlecomp_feedback_master(float *peak,float *rms){
  return pull_singlecomp_feedback(&master_state,peak,rms);
}

int pull_singlecomp_feedback_channel(float *peak,float *rms){
  return pull_singlecomp_feedback(&channel_state,peak,rms);
}

static void singlecomp_load_helper(singlecomp_state *scs,int ch){
  int i;
  memset(scs,0,sizeof(*scs));

  scs->ch=ch;
  scs->activeP=calloc(scs->ch,sizeof(*scs->activeP));
  scs->active0=calloc(scs->ch,sizeof(*scs->active0));

  scs->out.channels=scs->ch;
  scs->out.data=malloc(scs->ch*sizeof(*scs->out.data));
  for(i=0;i<scs->ch;i++)
    scs->out.data[i]=malloc(input_size*sizeof(**scs->out.data));

  scs->fillstate=0;
  scs->cache=malloc(scs->ch*sizeof(*scs->cache));
  for(i=0;i<scs->ch;i++)
    scs->cache[i]=malloc(input_size*sizeof(**scs->cache));

  scs->prevset=malloc(ch*sizeof(*scs->prevset));
  scs->currset=malloc(ch*sizeof(*scs->currset));
}

/* called only by initial setup */
int singlecomp_load(int outch){
  int i;
  singlecomp_load_helper(&master_state,outch);
  singlecomp_load_helper(&channel_state,input_ch);

  window=window_get(1,input_size/2);

  singlecomp_channel_set=calloc(input_ch,sizeof(*singlecomp_channel_set));
  master_set_bundle=malloc(outch*sizeof(*master_set_bundle));
  channel_set_bundle=malloc(input_ch*sizeof(*channel_set_bundle));
  for(i=0;i<input_ch;i++)
    channel_set_bundle[i]=&singlecomp_channel_set[i];
  for(i=0;i<outch;i++)
    master_set_bundle[i]=&singlecomp_master_set;

  return 0;
}

static void filter_set(float msec,
		       int order,
		       int attackp,
                       iir_filter *filter){
  float alpha;
  float corner_freq= 500./msec;
  
  /* make sure the chosen frequency doesn't require a lookahead
     greater than what's available */
  if(step_freq(input_size)*1.01>corner_freq && attackp && order==2)
    corner_freq=step_freq(input_size);
  
  alpha=corner_freq/input_rate;
  filter->g=mkbessel(alpha,order,filter->c);
  filter->alpha=alpha;
  filter->Hz=alpha*input_rate;
  filter->ms=msec;
}

static void reset_onech_filter(singlecomp_state *scs,int i){
  memset(scs->o_peak+i,0,sizeof(*scs->o_peak));
  memset(scs->u_peak+i,0,sizeof(*scs->u_peak));
  memset(scs->b_peak+i,0,sizeof(*scs->b_peak));

  /* all filters are set to 0, even the ones that steady-state at one,
     because we know that our steepest attack will complete in the
     pre-charge time, but there's no such guarantee about decay */
  memset(scs->o_iir+i,0,sizeof(*scs->o_iir));
  memset(scs->u_iir+i,0,sizeof(*scs->u_iir));
  memset(scs->b_iir+i,0,sizeof(*scs->b_iir));

  /* delays are only used for soft-starting individual filters when we
     know things began at unity multiplier */
  scs->o_delay[i]=0;
  scs->u_delay[i]=0;
  scs->b_delay[i]=0;
}

static void reset_filter(singlecomp_state *scs){
  int i;
  for(i=0;i<scs->ch;i++)
    reset_onech_filter(scs,i);
}

/* called only in playback thread */
int singlecomp_reset(void){
  /* reset cached pipe state */
  master_state.fillstate=0;
  channel_state.fillstate=0;
  while(pull_singlecomp_feedback_master(NULL,NULL));
  while(pull_singlecomp_feedback_channel(NULL,NULL));

  reset_filter(&master_state);
  reset_filter(&channel_state);
  return 0;
}

static void work_and_lapping(singlecomp_state *scs,
			     singlecomp_settings **scset,
			     time_linkage *in,
			     time_linkage *out,
			     int *active){
  int i;
  int have_feedback=0;
  u_int32_t mutemaskC=in->active;
  u_int32_t mutemask0=scs->mutemask0;
  u_int32_t mutemaskP=scs->mutemaskP;

  float peakfeed[scs->ch];
  float rmsfeed[scs->ch];
  memset(peakfeed,0,sizeof(peakfeed));
  memset(rmsfeed,0,sizeof(rmsfeed));
  
  for(i=0;i<scs->ch;i++){

    int o_active=0,u_active=0,b_active=0;

    int activeC= active[i] && !mute_channel_muted(mutemaskC,i);
    int active0= scs->active0[i];
    int activeP= scs->activeP[i];

    int mutedC=mute_channel_muted(mutemaskC,i);
    int muted0=mute_channel_muted(mutemask0,i);
    int mutedP=mute_channel_muted(mutemaskP,i);

    float o_attackms=scset[i]->o_attack*.1;
    float o_decayms=scset[i]->o_decay*.1;
    float u_attackms=scset[i]->u_attack*.1;
    float u_decayms=scset[i]->u_decay*.1;
    float b_attackms=scset[i]->b_attack*.1;
    float b_decayms=scset[i]->b_decay*.1;

    if(o_attackms!=scs->o_attack[i].ms)filter_set(o_attackms,2,1,&scs->o_attack[i]);
    if(o_decayms!=scs->o_decay[i].ms)filter_set(o_decayms,1,0,&scs->o_decay[i]);
    if(u_attackms!=scs->u_attack[i].ms)filter_set(u_attackms,2,1,&scs->u_attack[i]);
    if(u_decayms!=scs->u_decay[i].ms)filter_set(u_decayms,1,0,&scs->u_decay[i]);
    if(b_attackms!=scs->b_attack[i].ms)filter_set(b_attackms,2,1,&scs->b_attack[i]);
    if(b_decayms!=scs->b_decay[i].ms)filter_set(b_decayms,1,0,&scs->b_decay[i]);
    
    if(!active0 && !activeC){
      
      if(activeP) reset_onech_filter(scs,i); /* just became inactive;
                                                reset all filters for
                                                this channel */
      
      /* feedback */
      if(scset[i]->panel_visible){
	int k;
	float rms=0.;
	float peak=0.;
        float *x=scs->cache[i];
	have_feedback=1;

	if(!muted0){
	  for(k=0;k<input_size;k++){
	    float val=x[k]*x[k];
	    rms+= val;
	    if(peak<val)peak=val;
	  }
	}
	peakfeed[i]=todB_a(peak)*.5;
	rms/=input_size;
	rmsfeed[i]=todB_a(rms)*.5;
      }

      /* rotate data vectors */
      if(out){
	float *temp=out->data[i];
	out->data[i]=scs->cache[i];
	scs->cache[i]=temp;
      }
	

    }else if(active0 || activeC){
      float adj[input_size]; // under will set it
      atten_cache *prevset=scs->prevset+i;
      atten_cache *currset=scs->currset+i;

      currset->u_thresh=scset[i]->u_thresh;
      currset->o_thresh=scset[i]->o_thresh;
      currset->u_ratio=scset[i]->u_ratio;
      currset->o_ratio=scset[i]->o_ratio;
      currset->b_ratio=scset[i]->b_ratio;
      
      /* don't slew from an unknown value */
      if(!activeP || !scs->fillstate) 
	memcpy(prevset,currset,sizeof(*currset));
      
      /* don't run filters that will be applied at unity */
      if(prevset->u_ratio==1000 && currset->u_ratio==1000){
	scs->u_delay[i]=1;
	memset(scs->u_peak+i,0,sizeof(peak_state));
	memset(scs->u_iir+i,0,sizeof(iir_state));
      }else{
	if(scs->u_delay[i]-->0)currset->u_ratio=1000;
	if(scs->u_delay[i]<0)scs->u_delay[i]=0;
	u_active=1;
      }

      if(prevset->o_ratio==1000 && currset->o_ratio==1000){
	scs->o_delay[i]=1;
	memset(scs->o_peak+i,0,sizeof(peak_state));
	memset(scs->o_iir+i,0,sizeof(iir_state));
      }else{
	if(scs->o_delay[i]-->0)currset->o_ratio=1000;
	if(scs->o_delay[i]<0)scs->o_delay[i]=0;
	o_active=1;
      }

      if(prevset->b_ratio==1000 && currset->b_ratio==1000){
	scs->b_delay[i]=1;
	memset(scs->b_peak+i,0,sizeof(peak_state));
	memset(scs->b_iir+i,0,sizeof(iir_state));
      }else{
	if(scs->b_delay[i]-->0)currset->b_ratio=1000;
	if(scs->b_delay[i]<0)scs->b_delay[i]=0;
	b_active=1;
      }

      /* run the filters */
      memset(adj,0,sizeof(*adj)*input_size);

      if(u_active)
	bi_compand(scs->cache[i],in->data[i],adj,
		   //scs->prevset[i].u_thresh,
		   scs->currset[i].u_thresh,
		   1.f-1000./scs->prevset[i].u_ratio,
		   1.f-1000./scs->currset[i].u_ratio,
		   scset[i]->u_lookahead/1000.f,
		   scset[i]->u_mode,
		   scset[i]->u_softknee,
		   scs->u_attack+i,scs->u_decay+i,
		   scs->u_iir+i,scs->u_peak+i,
		   active0,0);
      
      if(o_active)
	bi_compand(scs->cache[i],in->data[i],adj,
		   //scs->prevset[i].o_thresh,
		   scs->currset[i].o_thresh,
		   1.f-1000.f/scs->prevset[i].o_ratio,
		   1.f-1000.f/scs->currset[i].o_ratio,
		   scset[i]->o_lookahead/1000.f,
		   scset[i]->o_mode,
		   scset[i]->o_softknee,
		   scs->o_attack+i,scs->o_decay+i,
		   scs->o_iir+i,scs->o_peak+i,
		   active0,1);
      

      /* feedback before base */
      if(scset[i]->panel_visible){
	int k;
	float rms=0.;
	float peak=0.;
        float *x=scs->cache[i];
	have_feedback=1;

	if(!muted0){
	  for(k=0;k<input_size;k++){
	    float mul=fromdB_a(adj[k]);
	    float val=x[k]*mul;
	  
	    val*=val;
	    rms+= val;
	    if(peak<val)peak=val;
	    
	  }
	}
	peakfeed[i]=todB_a(peak)*.5;
	rms/=input_size;
	rmsfeed[i]=todB_a(rms)*.5;
      }

      if(b_active)
	full_compand(scs->cache[i],in->data[i],adj,
		     1.-1000./scs->prevset[i].b_ratio,
		     1.-1000./scs->currset[i].b_ratio,
		     scset[i]->b_mode,
		     scs->b_attack+i,scs->b_decay+i,
		     scs->b_iir+i,scs->b_peak+i,
		     active0);

      if(active0 && out){
	/* current frame should be manipulated; render into out,
	   handle transitioning after */
	int k;
        float *ix=scs->cache[i];
        float *ox=out->data[i];

        for(k=0;k<input_size;k++)
          ox[k]=ix[k]*fromdB_a(adj[k]);

	/* is this frame preceeded/followed by an 'inactive' frame?
	   If so, smooth the transition */
	if(!activeP){
	  if(!mutedP){
	    for(k=0;k<input_size/2;k++){
	      float w=window[k];
	      ox[k]= ox[k]*w + ix[k]*(1.-w);
	    }
	  }
	}
	if(!activeC){
	  if(!mutedC){
	    float *cox=ox+input_size/2;
	    float *cix=ix+input_size/2;
	    for(k=0;k<input_size/2;k++){
	      float w=window[k];
	      cox[k]= cox[k]*(1.-w) + cix[k]*w;
	    }
	  }
	}
      }else if(out){
	float *temp=out->data[i];
	out->data[i]=scs->cache[i];
	scs->cache[i]=temp;
      }
    }
    {
      float *temp=scs->cache[i];
      scs->cache[i]=in->data[i];
      in->data[i]=temp;
    }
    scs->activeP[i]=active0;
    scs->active0[i]=activeC;

  }

  if(out){
    /* feedback is also triggered off of output */
    singlecomp_feedback *ff=
      (singlecomp_feedback *)feedback_new(&scs->feedpool,new_singlecomp_feedback);
    
    if(!ff->peak)
      ff->peak=malloc(scs->ch*sizeof(*ff->peak));
    
    if(!ff->rms)
      ff->rms=malloc(scs->ch*sizeof(*ff->rms));
    
    memcpy(ff->peak,peakfeed,sizeof(peakfeed));
    memcpy(ff->rms,rmsfeed,sizeof(rmsfeed));
    
    feedback_push(&scs->feedpool,(feedback_generic *)ff);

    out->active=mutemask0;
    out->samples=scs->cache_samples;
  }

  {
    atten_cache *temp=scs->prevset;
    scs->prevset=scs->currset;
    scs->currset=temp;
  }

  scs->cache_samples=in->samples;
  scs->mutemaskP=mutemask0;
  scs->mutemask0=mutemaskC;
}

time_linkage *singlecomp_read_helper(time_linkage *in,
				     singlecomp_state *scs, 
				     singlecomp_settings **scset,
				     int *active){
  int i;

  switch(scs->fillstate){
  case 0: /* prime the cache */
    if(in->samples==0){
      scs->out.samples=0;
      return &scs->out;
    }
    
    for(i=0;i<scs->ch;i++){
      memset(scs->o_iir+i,0,sizeof(*scs->o_iir));
      memset(scs->u_iir+i,0,sizeof(*scs->u_iir));
      memset(scs->b_iir+i,0,sizeof(*scs->b_iir));
      memset(scs->o_peak+i,0,sizeof(*scs->o_peak));
      memset(scs->u_peak+i,0,sizeof(*scs->u_peak));
      memset(scs->b_peak+i,0,sizeof(*scs->b_peak));
      memset(scs->cache[i],0,sizeof(**scs->cache)*input_size);
      scs->activeP[i]=scs->active0[i]=active[i];
    }
    scs->mutemaskP=scs->mutemask0=in->active;
    
    work_and_lapping(scs,scset,in,0,active);

    scs->fillstate=1;
    scs->out.samples=0;
    if(in->samples==input_size)goto tidy_up;
    
    for(i=0;i<scs->ch;i++)
      memset(in->data[i],0,sizeof(**in->data)*input_size);
    in->samples=0;
    /* fall through */
  case 1: /* nominal processing */

    work_and_lapping(scs,scset,in,&scs->out,active);

    if(scs->out.samples<input_size)scs->fillstate=2;
    break;
  case 2: /* we've pushed out EOF already */
    scs->out.samples=0;
  }
  
 tidy_up:
  {
    int tozero=input_size-scs->out.samples;
    if(tozero)
      for(i=0;i<scs->out.channels;i++)
        memset(scs->out.data[i]+scs->out.samples,0,sizeof(**scs->out.data)*tozero);
  }

  return &scs->out;
}

time_linkage *singlecomp_read_master(time_linkage *in){
  int active[master_state.ch],i;

  /* local copy required to avoid concurrency problems */
  for(i=0;i<master_state.ch;i++)
    active[i]=singlecomp_master_set.panel_active;

  return singlecomp_read_helper(in, &master_state, master_set_bundle,active);
}

time_linkage *singlecomp_read_channel(time_linkage *in){
  int active[channel_state.ch],i;

  /* local copy required to avoid concurrency problems */
  for(i=0;i<channel_state.ch;i++)
    active[i]=singlecomp_channel_set[i].panel_active;

  return singlecomp_read_helper(in, &channel_state, channel_set_bundle,active);
}
