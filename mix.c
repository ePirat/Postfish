/*
 *
 *  postfish
 *    
 *      Copyright (C) 2002-2005 Monty
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
#include "mix.h"
#include "window.h"

extern int input_ch;
extern int input_size;
extern int input_rate;

mix_settings *mix_set;

sig_atomic_t atten_visible;
sig_atomic_t *mixpanel_active;
sig_atomic_t *mixpanel_visible;


typedef struct{
  time_linkage out;
  feedback_generic_pool feedpool;

  mix_settings *prev;
  mix_settings *curr;

  float **cacheP;
  float **cachePP;

  float **cachePA;
  float **cachePPA;

  float **cachePB;
  float **cachePPB;

  int fillstate;
} mix_state;

mix_state ms;

/* this should be moved somewhere obvious/generic */
static float *window;

/* feedback! */
typedef struct limit_feedback{
  feedback_generic parent_class;
  float **peak;
  float **rms;
  int bypass;
} mix_feedback;

static feedback_generic *new_mix_feedback(void){
  mix_feedback *ret=calloc(1,sizeof(*ret));
  return (feedback_generic *)ret;
}

/* peak, rms are pulled in array[mixblock][input_ch] order */
int pull_mix_feedback(float **peak,float **rms){
  mix_feedback *f=(mix_feedback *)feedback_pull(&ms.feedpool);
  int i;
  
  if(!f)return 0;
  
  if(f->bypass){
    feedback_old(&ms.feedpool,(feedback_generic *)f);
    return 2;
  }else{
    if(peak)
      for(i=0;i<MIX_BLOCKS+5;i++)
        memcpy(peak[i],f->peak[i],sizeof(**peak)*input_ch);
    if(rms)
      for(i=0;i<MIX_BLOCKS+5;i++)
        memcpy(rms[i],f->rms[i],sizeof(**rms)*input_ch);
    feedback_old(&ms.feedpool,(feedback_generic *)f);
    return 1;
  }
}

/* called only by initial setup */
int mix_load(int outch){
  int i;

  mix_set=calloc(input_ch,sizeof(*mix_set));
  mixpanel_active=calloc(input_ch,sizeof(*mixpanel_active));
  mixpanel_visible=calloc(input_ch,sizeof(*mixpanel_visible));

  memset(&ms,0,sizeof(ms));
  ms.prev=calloc(input_ch,sizeof(*ms.prev));
  ms.curr=calloc(input_ch,sizeof(*ms.curr));
  
  ms.cacheP=malloc(input_ch*sizeof(*ms.cacheP));
  ms.cachePP=malloc(input_ch*sizeof(*ms.cachePP));
  ms.cachePA=malloc(input_ch*sizeof(*ms.cachePA));
  ms.cachePPA=malloc(input_ch*sizeof(*ms.cachePPA));
  ms.cachePB=malloc(input_ch*sizeof(*ms.cachePB));
  ms.cachePPB=malloc(input_ch*sizeof(*ms.cachePPB));
  for(i=0;i<input_ch;i++)
    ms.cacheP[i]=malloc(input_size*sizeof(**ms.cacheP));
  for(i=0;i<input_ch;i++)
    ms.cachePP[i]=malloc(input_size*sizeof(**ms.cachePP));
  for(i=0;i<input_ch;i++)
    ms.cachePA[i]=malloc(input_size*sizeof(**ms.cachePA));
  for(i=0;i<input_ch;i++)
    ms.cachePPA[i]=malloc(input_size*sizeof(**ms.cachePPA));
  for(i=0;i<input_ch;i++)
    ms.cachePB[i]=malloc(input_size*sizeof(**ms.cachePB));
  for(i=0;i<input_ch;i++)
    ms.cachePPB[i]=malloc(input_size*sizeof(**ms.cachePPB));

  ms.out.channels=outch;
  ms.out.data=malloc(outch*sizeof(*ms.out.data));
  for(i=0;i<outch;i++)
    ms.out.data[i]=malloc(input_size*sizeof(**ms.out.data));

  window=window_get(1,input_size);

  return 0;
}

/* called only in playback thread */
int mix_reset(){
  while(pull_mix_feedback(NULL,NULL));
  ms.fillstate=0;
  return 0;
}

static void mixwork(float *data,float *cacheP,float *cachePP,
		    float *out,
		    float att,int del,int inv,
		    float attP,float delP,int invP){ 
  int offset=0;
  int i;

  if(-del>input_size*2)del= -input_size*2;
  if(-delP>input_size*2)delP= -input_size*2;
  
  if(inv)att*=-1.;
  if(invP)attP*=-1.;

  if(att==attP && del==delP){

    /* straight copy from cache with attenuation */
    i=input_size*2+del;
    while(i<input_size && offset<input_size)
      out[offset++]+=cachePP[i++]*att;
    
    i=input_size+del;
    if(i<0)i=0;
    while(i<input_size && offset<input_size)
      out[offset++]+=cacheP[i++]*att;
    
    i=0;
    while(offset<input_size)
      out[offset++]+=data[i++]*att;

  }else{
    /* lapped dual copy from cache */
    
    /* current settings */
    i=input_size*2+del;
    while(i<input_size && offset<input_size){
      out[offset]+=cachePP[i++]*att*window[offset];
      offset++;
    }

    i=input_size+del;
    if(i<0)i=0;
    while(i<input_size && offset<input_size){
      out[offset]+=cacheP[i++]*att*window[offset];
      offset++;
    }

    i=0;
    while(offset<input_size){
      out[offset]+=data[i++]*att*window[offset];
      offset++;
    }

    /* ...lapped transition from old settings */
    offset=0;
    i=input_size*2+delP;
    while(i<input_size && offset<input_size){
      out[offset]+=cachePP[i++]*attP*window[input_size-offset-1];
      offset++;
    }

    i=input_size+delP;
    if(i<0)i=0;
    while(i<input_size && offset<input_size){
      out[offset]+=cacheP[i++]*attP*window[input_size-offset-1];
      offset++;
    }

    i=0;
    while(offset<input_size){
      out[offset]+=data[i++]*attP*window[input_size-offset-1];
      offset++;
    }
  }
}

/* smooth active/inactive transitions while adding */
static void mixadd(float *in,float *out,int active,int activeP){
  int i;
  if(!active && !activeP)return;
  if(active && activeP){
    for(i=0;i<input_size;i++)
      out[i]+=in[i];
    return;
  }
  /* mutes no longer need be transitioned */
}

/* called only by playback thread */
time_linkage *mix_read(time_linkage *in, 
		       time_linkage *inA,  // reverb channel 
		       time_linkage *inB){ // reverb channel

  int i,j,k,outch=ms.out.channels;  
  int outactive[outch];

  float peak[MIX_BLOCKS+5][input_ch];
  float rms[MIX_BLOCKS+5][input_ch];
  int bypass=1;

  if(in->samples==0){
    ms.out.samples=0;
    return &ms.out;
  }
  memset(outactive,0,sizeof(outactive));
  memset(peak,0,sizeof(peak));
  memset(rms,0,sizeof(rms));

  /* eliminate asynch change possibility */
  memcpy(ms.curr,mix_set,sizeof(*mix_set)*input_ch);

  /* fillstate here is only used for lazy initialization/reset */
  if(ms.fillstate==0){
    /* zero the cache */
    for(i=0;i<input_ch;i++){
      memset(ms.cacheP[i],0,sizeof(**ms.cacheP)*input_size);
      memset(ms.cachePP[i],0,sizeof(**ms.cachePP)*input_size);
      memset(ms.cachePA[i],0,sizeof(**ms.cachePA)*input_size);
      memset(ms.cachePPA[i],0,sizeof(**ms.cachePPA)*input_size);
      memset(ms.cachePB[i],0,sizeof(**ms.cachePB)*input_size);
      memset(ms.cachePPB[i],0,sizeof(**ms.cachePPB)*input_size);
    }
    memcpy(ms.prev,ms.curr,sizeof(*mix_set)*input_ch);
    ms.fillstate=1;
  }

  /* zero the output block; we'll me mixing into it input-by-input */
  for(i=0;i<outch;i++)
    memset(ms.out.data[i],0,sizeof(**ms.out.data)*input_size);

  /* a bit of laziness that may actually save CPU time by avoiding
     special-cases later */
  for(i=0;i<input_ch;i++){
    if(mute_channel_muted(in->active,i))
      memset(in->data[i],0,sizeof(**in->data)*input_size);
    if(mute_channel_muted(inA->active,i))
      memset(inA->data[i],0,sizeof(**inA->data)*input_size);
    if(mute_channel_muted(inB->active,i))
      memset(inB->data[i],0,sizeof(**inB->data)*input_size);
  }

    /* input-by-input */
  for(i=0;i<input_ch;i++){
    int feedit=mixpanel_visible[i];
    int feeditM=atten_visible;

    /* master feedback is a bit of a pain; the metrics we need aren't
       produced by any of the mixdowns below. Do it by hand */
    if(feeditM){
      float mix[input_size];
      float att=fromdB(ms.curr[i].master_att * .1);
      int del=rint(ms.curr[i].master_delay*.00001*input_rate);
      float acc=0.;

      if(!mute_channel_muted(in->active,i)){
	memset(mix,0,sizeof(mix));
	mixwork(in->data[i],ms.cacheP[i],ms.cachePP[i],
		mix,att,del,0,att,del,0);
	
	bypass=0;
	for(j=0;j<input_size;j++){
	  float val=mix[j]*mix[j];
	  if(val>peak[0][i])peak[0][i]=val;
	  acc+=val;
	}
	
	rms[0][i]=acc/input_size;
      }

      acc=0.;
      if(inA && !mute_channel_muted(inA->active,i)){
	memset(mix,0,sizeof(mix));
	mixwork(inA->data[i],ms.cachePA[i],ms.cachePPA[i],
		mix,att,del,0,att,del,0);
	
	bypass=0;
	for(j=0;j<input_size;j++){
	  float val=mix[j]*mix[j];
	  if(val>peak[1][i])peak[1][i]=val;
	  acc+=val;
	}
	
	rms[1][i]=acc/input_size;
      }

      acc=0.;
      if(inB && !mute_channel_muted(inB->active,i)){
	memset(mix,0,sizeof(mix));
	mixwork(inB->data[i],ms.cachePB[i],ms.cachePPB[i],
		mix,att,del,0,att,del,0);
	
	bypass=0;
	for(j=0;j<input_size;j++){
	  float val=mix[j]*mix[j];
	  if(val>peak[2][i])peak[2][i]=val;
	  acc+=val;
	}
	
	rms[2][i]=acc/input_size;
      }
    }

    /* placer settings; translate to final numbers */
    int placer=ms.curr[i].placer_place;
    int placerP=ms.prev[i].placer_place;

    float relA=(placer>100 ? placer*.01-1. : 0.);
    float relB=(placer<100 ? 1.-placer*.01 : 0.);
    float relAP=(placerP>100 ? placerP*.01-1. : 0.);
    float relBP=(placerP<100 ? 1.-placerP*.01 : 0.);

    float attA=
      fromdB((ms.curr[i].master_att +
	      ms.curr[i].placer_att * relA)*.1);
    
    float attB=
      fromdB((ms.curr[i].master_att +
	      ms.curr[i].placer_att * relB)*.1);
    
    int delA=
      rint((ms.curr[i].master_delay +
	    ms.curr[i].placer_delay * relA)*.00001*input_rate);

    int delB=
      rint((ms.curr[i].master_delay +
	    ms.curr[i].placer_delay * relB)*.00001*input_rate);

    float attAP=
      fromdB((ms.prev[i].master_att +
	      ms.prev[i].placer_att * relAP)*.1);
    
    float attBP=
      fromdB((ms.prev[i].master_att +
	      ms.prev[i].placer_att * relBP)*.1);
    
    int delAP=
      rint((ms.prev[i].master_delay +
	    ms.prev[i].placer_delay * relAP)*.00001*input_rate);

    int delBP=
      rint((ms.prev[i].master_delay +
	    ms.prev[i].placer_delay * relBP)*.00001*input_rate);

    /* place mix */
    {
      int mixedA=0,mixedB=0;
      float mixA[input_size],mixB[input_size];

      for(j=0;j<OUTPUT_CHANNELS;j++){
	int destA=ms.curr[i].placer_destA[j];
	int destAP=ms.prev[i].placer_destA[j];
	int destB=ms.curr[i].placer_destB[j];
	int destBP=ms.prev[i].placer_destB[j];
	
	if(destA || destAP){
	  outactive[j]=1;
	  
	  if(!mixedA){
	    memset(mixA,0,sizeof(mixA));
	    mixwork(in->data[i],ms.cacheP[i],ms.cachePP[i],
		    mixA,attA,delA,0,attAP,delAP,0);
	    mixedA=1;
	  }
	  mixadd(mixA,ms.out.data[j],destA,destAP);
	}
	if(destB || destBP){
	  outactive[j]=1;
	  
	  if(!mixedB){
	    memset(mixB,0,sizeof(mixB));
	    mixwork(in->data[i],ms.cacheP[i],ms.cachePP[i],
		    mixB,attB,delB,0,attBP,delBP,0);
	    mixedB=1;
	  }
	  mixadd(mixB,ms.out.data[j],destB,destBP);
	}
      }
      
      /* feedback for A */
      if(feedit){
	float acc=0.;
	bypass=0;
	if(mixedA){
	  for(j=0;j<input_size;j++){
	    float val=mixA[j]*mixA[j];
	    if(val>peak[3][i])peak[3][i]=val;
	    acc+=val;
	  }
	  
	  peak[3][i]=peak[3][i];
	  rms[3][i]=acc/input_size;
	}
      }

      /* feedback for B */
      if(feedit){
	float acc=0.;
	bypass=0;
	if(mixedB){
	  for(j=0;j<input_size;j++){
	    float val=mixB[j]*mixB[j];
	    if(val>peak[4][i])peak[4][i]=val;
	    acc+=val;
	  }
	  
	  peak[4][i]=peak[4][i];
	  rms[4][i]=acc/input_size;
	}
      }
    }

    /* direct block mix */
    for(k=0;k<MIX_BLOCKS;k++){
      float mix[input_size];

      int sourceM=ms.curr[i].insert_source[k][0];
      int sourceMP=ms.prev[i].insert_source[k][0];
      int sourceA=ms.curr[i].insert_source[k][1];
      int sourceAP=ms.prev[i].insert_source[k][1];
      int sourceB=ms.curr[i].insert_source[k][2];
      int sourceBP=ms.prev[i].insert_source[k][2];

      float att=
	fromdB((ms.curr[i].master_att +
		ms.curr[i].insert_att[k])*.1);
      
      int del=
	rint((ms.curr[i].master_delay +
	      ms.curr[i].insert_delay[k])*.00001*input_rate);

      float attP=
	fromdB((ms.prev[i].master_att +
		ms.prev[i].insert_att[k])*.1);
      
      int delP=
	rint((ms.prev[i].master_delay +
	      ms.prev[i].insert_delay[k])*.00001*input_rate);

      if(sourceM || sourceMP || 
	 sourceA || sourceAP ||
	 sourceB || sourceBP){
	memset(mix,0,sizeof(mix));

	/* master */
	if(sourceM || sourceMP)
	  mixwork(in->data[i],ms.cacheP[i],ms.cachePP[i],
		  mix,
		  (sourceM ? att : 0),
		  del,ms.curr[i].insert_invert[k],
		  (sourceMP ? attP : 0),
		  delP,ms.prev[i].insert_invert[k]);

	/* reverbA */
	if(sourceA || sourceAP)
	  if(inA)
	    mixwork(inA->data[i],ms.cachePA[i],ms.cachePPA[i],
		    mix,
		    (sourceA ? att : 0),
		    del,ms.curr[i].insert_invert[k],
		    (sourceAP ? attP : 0),
		    delP,ms.prev[i].insert_invert[k]);

	/* reverbB */
	if(sourceB || sourceBP)
	  if(inB)
	    mixwork(inB->data[i],ms.cachePB[i],ms.cachePPB[i],
		    mix,
		    (sourceB ? att : 0),
		    del,ms.curr[i].insert_invert[k],
		    (sourceBP ? attP : 0),
		    delP,ms.prev[i].insert_invert[k]);

	/* mix into output */
	for(j=0;j<OUTPUT_CHANNELS;j++){
	  int dest=ms.curr[i].insert_dest[k][j];
	  int destP=ms.prev[i].insert_dest[k][j];
	  
	  if(dest || destP){
	    outactive[j]=1;	    
	    mixadd(mix,ms.out.data[j],dest,destP);
	  }
	}
	
	/* feedback */
	if(feedit){
	  float acc=0.;
	  bypass=0;
	  for(j=0;j<input_size;j++){
	    float val=mix[j]*mix[j];
	    if(val>peak[5+k][i])peak[5+k][i]=val;
	    acc+=val;
	  }
	  
	  peak[5+k][i]=peak[5+k][i];
	  rms[5+k][i]=acc/input_size;

	}
      }
    }
    /* rotate data cache */
    {
      float *temp=ms.cachePP[i];
      ms.cachePP[i]=ms.cacheP[i];
      ms.cacheP[i]=in->data[i];
      in->data[i]=temp;

      if(inA){
	temp=ms.cachePPA[i];
	ms.cachePPA[i]=ms.cachePA[i];
	ms.cachePA[i]=inA->data[i];
	inA->data[i]=temp;
      }

      if(inB){
	temp=ms.cachePPB[i];
	ms.cachePPB[i]=ms.cachePB[i];
	ms.cachePB[i]=inB->data[i];
	inB->data[i]=temp;
      }
    }
  }
  
  /* finish output data */
  ms.out.samples=in->samples;
  ms.out.active=0;
  for(i=0;i<OUTPUT_CHANNELS;i++)
    if(outactive[i])
      ms.out.active|=(1<<i);
  
  /* rotate settings cache */
  {
    mix_settings *temp=ms.curr;
    ms.curr=ms.prev;
    ms.prev=temp;
  }

  /* push feedback */
  if(bypass){
    mix_feedback *mf=
      (mix_feedback *)feedback_new(&ms.feedpool,new_mix_feedback);
    mf->bypass=1;
    feedback_push(&ms.feedpool,(feedback_generic *)mf);
  }else{
    mix_feedback *mf=
      (mix_feedback *)feedback_new(&ms.feedpool,new_mix_feedback);
    
    if(!mf->peak){
      mf->peak=malloc((MIX_BLOCKS+5)*sizeof(*mf->peak));
      mf->rms=malloc((MIX_BLOCKS+5)*sizeof(*mf->rms));
  
      for(i=0;i<MIX_BLOCKS+5;i++)
        mf->rms[i]=malloc(input_ch*sizeof(**mf->rms));
      for(i=0;i<MIX_BLOCKS+5;i++)
        mf->peak[i]=malloc(input_ch*sizeof(**mf->peak));
    }
    
    for(i=0;i<MIX_BLOCKS+5;i++){
      memcpy(mf->peak[i],peak[i],input_ch*sizeof(**peak));
      memcpy(mf->rms[i],rms[i],input_ch*sizeof(**rms));
    } 
    mf->bypass=0;
    feedback_push(&ms.feedpool,(feedback_generic *)mf);
  }

  return &ms.out;
}

