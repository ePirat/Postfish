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
#include "limit.h"

extern int input_size;
extern int input_rate;
extern int input_ch;

sig_atomic_t limit_active;
sig_atomic_t limit_visible;

typedef struct{
  time_linkage out;
  feedback_generic_pool feedpool;

  iir_state *iir;
  iir_filter decay;

} limit_state;

limit_settings limitset;
limit_state limitstate;

static void _analysis(char *base,int i,float *v,int n,int dB,int offset){
  int j;
  FILE *of;
  char buffer[80];

  sprintf(buffer,"%s_%d.m",base,i);
  of=fopen(buffer,"a");
  
  if(!of)perror("failed to open data dump file");
  
  for(j=0;j<n;j++){
    fprintf(of,"%f ",(float)j+offset);
    if(dB)
      fprintf(of,"%f\n",todB(v[j]));
    else
      fprintf(of,"%f\n",(v[j]));
  }
  fprintf(of,"\n");
  fclose(of);
}

static int offset=0;

/* feedback! */
typedef struct limit_feedback{
  feedback_generic parent_class;
  float *peak;
  float *att;
} limit_feedback;

static feedback_generic *new_limit_feedback(void){
  limit_feedback *ret=calloc(1,sizeof(*ret));
  return (feedback_generic *)ret;
}


int pull_limit_feedback(float *peak,float *att){
  limit_feedback *f=(limit_feedback *)feedback_pull(&limitstate.feedpool);
  
  if(!f)return 0;
  
  if(peak)
    memcpy(peak,f->peak,sizeof(*peak)*input_ch);
  if(att)
    memcpy(att,f->att,sizeof(*att)*input_ch);
  feedback_old(&limitstate.feedpool,(feedback_generic *)f);
  return 1;
}

/* called only by initial setup */
int limit_load(void){
  int i;
  memset(&limitstate,0,sizeof(limitstate));

  limitstate.iir=calloc(input_ch,sizeof(*limitstate.iir));
  limitstate.out.size=input_size;
  limitstate.out.channels=input_ch;
  limitstate.out.rate=input_rate;
  limitstate.out.data=malloc(input_ch*sizeof(*limitstate.out.data));
  for(i=0;i<input_ch;i++)
    limitstate.out.data[i]=malloc(input_size*sizeof(**limitstate.out.data));

  return(0);
}

static void filter_set(float msec,
                       iir_filter *filter){
  float alpha;
  float corner_freq= 500./msec;
  
  alpha=corner_freq/input_rate;
  filter->g=mkbessel(alpha,2,filter->c);
  filter->alpha=alpha;
  filter->Hz=alpha*input_rate;
  filter->ms=msec;
}

/* called only in playback thread */
int limit_reset(void ){
  /* reset cached pipe state */
  while(pull_limit_feedback(NULL,NULL));
  memset(limitstate.iir,0,input_ch*sizeof(&limitstate.iir));
  return 0;
}

static inline float limit_knee(float x,float d){
  return (sqrtf(x*x+d)-x)*-.5f;
}


time_linkage *limit_read(time_linkage *in){
  float peakfeed[input_ch];
  float attfeed[input_ch];

  int active=limit_active;
  int visible=limit_visible;
  int bypass=!(active || visible);
  int i,k;

  float thresh=limitset.thresh/10.-.01;
  float depth=limitset.depth;
  float localpeak;
  float localatt;

  float decayms=limitset.decay*.1;
  if(decayms!=limitstate.decay.ms)filter_set(decayms,&limitstate.decay);

  if(in->samples==0){
    limitstate.out.samples=0;
    return &limitstate.out;
  }

  depth=depth*.2;
  depth*=depth;

  for(i=0;i<input_ch;i++){
    localpeak=0.;
    localatt=0.;
    
    if(!bypass){
      float *inx=in->data[i];
      float *x=limitstate.out.data[i];
      
      if(active){

	/* 'knee' the actual samples, compute attenuation depth */
	for(k=0;k<in->samples;k++){
	  float dB=todB(inx[k]);
	  float knee=limit_knee(dB-thresh,depth)+thresh;
	  float att=dB-knee;

	  if(att>localatt)localatt=att;

	  x[k]=att;
	}
	

	compute_iir_freefall2(x,input_size,limitstate.iir+i,&limitstate.decay);

	
	for(k=0;k<in->samples;k++)
	  x[k]=inx[k]*fromdB(-x[k]);
     
	
      }
    }
    
    if(!active){
      float *temp=in->data[i];
      in->data[i]=limitstate.out.data[i];
      limitstate.out.data[i]=temp;
    }

    if(!bypass){
      float *x=limitstate.out.data[i];
      /* get peak feedback */
      for(k=0;k<in->samples;k++)
	if(fabs(x[k])>localpeak)localpeak=fabs(x[k]);

    }

    peakfeed[i]=todB(localpeak);
    attfeed[i]=localatt;
    
  }

  limitstate.out.samples=in->samples;
  
  /* finish up the state feedabck */
  {
    limit_feedback *ff=
      (limit_feedback *)feedback_new(&limitstate.feedpool,new_limit_feedback);
    
    if(!ff->peak)
      ff->peak=malloc(input_ch*sizeof(*ff->peak));
    
    if(!ff->att)
      ff->att=malloc(input_ch*sizeof(*ff->att));
    
    memcpy(ff->peak,peakfeed,sizeof(peakfeed));
    memcpy(ff->att,attfeed,sizeof(attfeed));

    feedback_push(&limitstate.feedpool,(feedback_generic *)ff);
  }
   
  {
    int tozero=limitstate.out.size-limitstate.out.samples;
    if(tozero)
      for(i=0;i<limitstate.out.channels;i++)
        memset(limitstate.out.data[i]+limitstate.out.samples,0,sizeof(**limitstate.out.data)*tozero);
  }

  offset+=input_size;

  return &limitstate.out;
}

