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
#include "bessel.h"
#include "singlecomp.h"

extern int input_size;
extern int input_rate;
extern int input_ch;

sig_atomic_t singlecomp_active;
sig_atomic_t singlecomp_visible;

typedef struct {
  int loc;
  float val;
} peak_state;

typedef struct{
  time_linkage out;
  feedback_generic_pool feedpool;

  iir_state *o_iir;
  iir_state *u_iir;
  iir_state *b_iir;

  peak_state *o_peak;
  peak_state *u_peak;
  peak_state *b_peak;

  iir_filter o_attack;
  iir_filter o_decay;
  iir_filter u_attack;
  iir_filter u_decay;
  iir_filter b_attack;
  iir_filter b_decay;

  int fillstate;
  float **cache;
  int cache_samples;

  u_int32_t mutemask0;

} singlecomp_state;

singlecomp_settings scset;
singlecomp_state scs;

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

int pull_singlecomp_feedback(float *peak,float *rms){
  singlecomp_feedback *f=(singlecomp_feedback *)feedback_pull(&scs.feedpool);
  
  if(!f)return 0;
  
  if(peak)
    memcpy(peak,f->peak,sizeof(*peak)*input_ch);
  if(rms)
    memcpy(rms,f->rms,sizeof(*rms)*input_ch);
  feedback_old(&scs.feedpool,(feedback_generic *)f);
  return 1;
}

/* called only by initial setup */
int singlecomp_load(void){
  int i;
  memset(&scs,0,sizeof(scs));

  scs.o_iir=calloc(input_ch,sizeof(*scs.o_iir));
  scs.b_iir=calloc(input_ch,sizeof(*scs.b_iir));
  scs.u_iir=calloc(input_ch,sizeof(*scs.u_iir));

  scs.o_peak=calloc(input_ch,sizeof(*scs.o_peak));
  scs.b_peak=calloc(input_ch,sizeof(*scs.b_peak));
  scs.u_peak=calloc(input_ch,sizeof(*scs.u_peak));

  scs.out.size=input_size;
  scs.out.channels=input_ch;
  scs.out.rate=input_rate;
  scs.out.data=malloc(input_ch*sizeof(*scs.out.data));
  for(i=0;i<input_ch;i++)
    scs.out.data[i]=malloc(input_size*sizeof(**scs.out.data));

  scs.fillstate=0;
  scs.cache=malloc(input_ch*sizeof(*scs.cache));
  for(i=0;i<input_ch;i++)
    scs.cache[i]=malloc(input_size*sizeof(**scs.cache));

  return(0);
}

static void filter_set(float msec,
                       iir_filter *filter,
                       int attackp){
  float alpha;
  float corner_freq= 500./msec;
  
  /* make sure the chosen frequency doesn't require a lookahead
     greater than what's available */
  if(step_freq(input_size)*1.01>corner_freq && attackp)
    corner_freq=step_freq(input_size);
  
  alpha=corner_freq/input_rate;
  filter->g=mkbessel(alpha,2,filter->c);
  filter->alpha=alpha;
  filter->Hz=alpha*input_rate;
  filter->ms=msec;
}

/* called only in playback thread */
int singlecomp_reset(void ){
  /* reset cached pipe state */
  scs.fillstate=0;
  while(pull_singlecomp_feedback(NULL,NULL));

  memset(scs.o_peak,0,input_ch*sizeof(&scs.o_peak));
  memset(scs.u_peak,0,input_ch*sizeof(&scs.u_peak));
  memset(scs.b_peak,0,input_ch*sizeof(&scs.b_peak));
  memset(scs.o_iir,0,input_ch*sizeof(&scs.o_iir));
  memset(scs.u_iir,0,input_ch*sizeof(&scs.u_iir));
  memset(scs.b_iir,0,input_ch*sizeof(&scs.b_iir));
  return 0;
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

static void run_filter(float *cache, float *in, float *work,
                       int ahead,int hold,int mode,
                       iir_state *iir,iir_filter *attack,iir_filter *decay,
                       peak_state *ps){
  int k;
  float *work2=work+input_size;

  if(mode){
    /* peak mode */
    memcpy(work,cache,sizeof(*work)*input_size);
    memcpy(work2,in,sizeof(*work)*input_size);

    prepare_peak(work, work, input_size, ahead, hold, ps);

  }else{
    /* rms mode */
    float *cachea=cache+ahead;
    float *worka=work+input_size-ahead;
    
    for(k=0;k<input_size-ahead;k++)
      work[k]=cachea[k]*cachea[k];
    
    for(k=0;k<ahead;k++)
      worka[k]=in[k]*in[k];    
  }
  
  compute_iir2(work, input_size, iir, attack, decay);
  
  if(mode==0)
    for(k=0;k<input_size;k++)
      work[k]=todB_a(work+k)*.5f;
  else
    for(k=0;k<input_size;k++)
      work[k]=todB_a(work+k);
}

static float soft_knee(float x){
  return (sqrtf(x*x+30.f)+x)*-.5f;
}

static float hard_knee(float x){
  return (x>0.f?-x:0.f);
}

static void over_compand(float *A,float *B,float *adj,
			 float zerocorner,float multiplier,
			 float lookahead,int mode,int softknee,
                         iir_filter *attack, iir_filter *decay,
                         iir_state *iir, peak_state *ps,
			 int active){
  
  int k;
  float work[input_size*2];
  int ahead=(mode?step_ahead(attack->alpha):impulse_ahead2(attack->alpha));
  int hold=(1.-lookahead)*ahead;
  ahead-=hold;

  run_filter(A,B,work,ahead,hold,mode,iir,attack,decay,ps);
  
  if(active){
    if(softknee){
      for(k=0;k<input_size;k++)
        adj[k]+=soft_knee(work[k]-zerocorner)*multiplier;
    }else{
      for(k=0;k<input_size;k++)
        adj[k]+=hard_knee(work[k]-zerocorner)*multiplier;
    }
  }
}

static void under_compand(float *A,float *B,float *adj,
			  float zerocorner,float multiplier,
			  float lookahead,int mode,int softknee,
			  iir_filter *attack, iir_filter *decay,
			  iir_state *iir, peak_state *ps,
			  int active){
  int k;
  float work[input_size*2];
  int ahead=(mode?step_ahead(attack->alpha):impulse_ahead2(attack->alpha));
  int hold=(1.-lookahead)*ahead;
  ahead-=hold;
  
  run_filter(A,B,work,ahead,hold,mode,iir,attack,decay,ps);

  if(active){
    if(softknee){
      for(k=0;k<input_size;k++)
        adj[k]= -soft_knee(zerocorner-work[k])*multiplier;
    }else{
      for(k=0;k<input_size;k++)
        adj[k]= -hard_knee(zerocorner-work[k])*multiplier;
    }
  }else
    memset(adj,0,sizeof(*adj)*input_size);

}

static void base_compand(float *A,float *B,float *adj,
			 float multiplier,int mode,
			 iir_filter *attack, iir_filter *decay,
			 iir_state *iir, peak_state *ps,
			 int active){
  
  int k;
  float work[input_size*2];

  int ahead=(mode?step_ahead(attack->alpha):impulse_ahead2(attack->alpha));

  run_filter(A,B,work,ahead,0,mode,iir,attack,decay,ps);

  if(active)
    for(k=0;k<input_size;k++)
      adj[k]-=(work[k]+adj[k])*multiplier;

}

time_linkage *singlecomp_read(time_linkage *in){
  float peakfeed[input_ch];
  float rmsfeed[input_ch];

  int active=singlecomp_active;
  int i;

  float o_attackms=scset.o_attack*.1;
  float o_decayms=scset.o_decay*.1;
  float u_attackms=scset.u_attack*.1;
  float u_decayms=scset.u_decay*.1;
  float b_attackms=scset.b_attack*.1;
  float b_decayms=scset.b_decay*.1;

  if(o_attackms!=scs.o_attack.ms)filter_set(o_attackms,&scs.o_attack,1);
  if(o_decayms!=scs.o_decay.ms)filter_set(o_decayms,&scs.o_decay,0);
  if(u_attackms!=scs.u_attack.ms)filter_set(u_attackms,&scs.u_attack,1);
  if(u_decayms!=scs.u_decay.ms)filter_set(u_decayms,&scs.u_decay,0);
  if(b_attackms!=scs.b_attack.ms)filter_set(b_attackms,&scs.b_attack,1);
  if(b_decayms!=scs.b_decay.ms)filter_set(b_decayms,&scs.b_decay,0);
  
  switch(scs.fillstate){
  case 0: /* prime the cache */
    if(in->samples==0){
      scs.out.samples=0;
      return &scs.out;
    }
    scs.mutemask0=in->active;

    for(i=0;i<input_ch;i++){
      float *temp=in->data[i];
      float adj[input_size]; // under will set it

      memset(scs.o_iir+i,0,sizeof(*scs.o_iir));
      memset(scs.u_iir+i,0,sizeof(*scs.u_iir));
      memset(scs.b_iir+i,0,sizeof(*scs.b_iir));
      memset(scs.cache[i],0,sizeof(**scs.cache)*input_size);

      under_compand(scs.cache[i],in->data[i],adj,
		    (float)(scset.u_thresh),
		    1.-1./(scset.u_ratio/1000.),
		    scset.u_lookahead/1000.,
		    scset.u_mode,
		    scset.u_softknee,
		    &scs.u_attack,&scs.u_decay,
		    scs.u_iir+i,scs.u_peak+i,
		    active);
      
      over_compand(scs.cache[i],in->data[i],adj,
		   (float)(scset.o_thresh),
		   1.-1./(scset.o_ratio/1000.),
		   scset.o_lookahead/1000.,
		   scset.o_mode,
		   scset.o_softknee,
		   &scs.o_attack,&scs.o_decay,
		   scs.o_iir+i,scs.o_peak+i,
		   active);

      base_compand(scs.cache[i],in->data[i],adj,
		   1.-1./(scset.b_ratio/1000.),
		   scset.b_mode,
		   &scs.b_attack,&scs.b_decay,
		   scs.b_iir+i,scs.b_peak+i,
		   active);


      in->data[i]=scs.cache[i];
      scs.cache[i]=temp;
    }
    scs.cache_samples=in->samples;
    scs.fillstate=1;
    scs.out.samples=0;
    if(in->samples==in->size)goto tidy_up;
    
    for(i=0;i<input_ch;i++)
      memset(in->data[i],0,sizeof(**in->data)*in->size);
    in->samples=0;
    /* fall through */
  case 1: /* nominal processing */

    for(i=0;i<input_ch;i++){
      
      float adj[input_size]; // under will set it
      
      under_compand(scs.cache[i],in->data[i],adj,
		    (float)(scset.u_thresh),
		    1.-1./(scset.u_ratio/1000.),
		    scset.u_lookahead/1000.,
		    scset.u_mode,
		    scset.u_softknee,
		    &scs.u_attack,&scs.u_decay,
		    scs.u_iir+i,scs.u_peak+i,
		    active);
      
      over_compand(scs.cache[i],in->data[i],adj,
		   (float)(scset.o_thresh),
		   1.-1./(scset.o_ratio/1000.),
		   scset.o_lookahead/1000.,
		   scset.o_mode,
		   scset.o_softknee,
		   &scs.o_attack,&scs.o_decay,
		   scs.o_iir+i,scs.o_peak+i,
		   active);

      /* feedback before base */
      {
	int k;
	float rms=0.;
	float peak=0.;
        float *x=scs.cache[i];

	for(k=0;k<input_size;k++){
	  float mul=fromdB_a(adj[k]);
	  float val=x[k]*mul;
	  
	  val*=val;
	  rms+= val;
	  if(peak<val)peak=val;

	}

	peakfeed[i]=todB_a(&peak)*.5;
	rms/=input_size;
	rmsfeed[i]=todB_a(&rms)*.5;
      }
      
      base_compand(scs.cache[i],in->data[i],adj,
		   1.-1./(scset.b_ratio/1000.),
		   scset.b_mode,
		   &scs.b_attack,&scs.b_decay,
		   scs.b_iir+i,scs.b_peak+i,
		   active);

      if(active){
	int k;
        float *x=scs.cache[i];
        float *out=scs.out.data[i];

        for(k=0;k<input_size;k++)
          out[k]=x[k]*fromdB_a(adj[k]);
      }else
	memcpy(scs.out.data[i],scs.cache[i],input_size*sizeof(*scs.cache[i]));

      {
	float *temp=scs.cache[i];
	scs.cache[i]=in->data[i];
	in->data[i]=temp;
      }
    }
    scs.out.samples=scs.cache_samples;
    scs.cache_samples=in->samples;
    if(scs.out.samples<scs.out.size)scs.fillstate=2;
    break;
  case 2: /* we've pushed out EOF already */
    scs.out.samples=0;
  }
  
  /* finish up the state feedabck */
  {
    singlecomp_feedback *ff=
      (singlecomp_feedback *)feedback_new(&scs.feedpool,new_singlecomp_feedback);
    
    if(!ff->peak)
      ff->peak=malloc(input_ch*sizeof(*ff->peak));
    
    if(!ff->rms)
      ff->rms=malloc(input_ch*sizeof(*ff->rms));

    memcpy(ff->peak,peakfeed,sizeof(peakfeed));
    memcpy(ff->rms,rmsfeed,sizeof(rmsfeed));

    feedback_push(&scs.feedpool,(feedback_generic *)ff);
  }
   
 tidy_up:
  {
    int tozero=scs.out.size-scs.out.samples;
    if(tozero)
      for(i=0;i<scs.out.channels;i++)
        memset(scs.out.data[i]+scs.out.samples,0,sizeof(**scs.out.data)*tozero);
  }

  scs.out.active=scs.mutemask0;
  scs.mutemask0=in->active;
  return &scs.out;
}

