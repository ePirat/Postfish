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
#include <math.h>
#include <sys/types.h>
#include <fftw3.h>
#include "reconstruct.h"
#include "feedback.h"

extern int input_rate;
extern int input_ch;
extern int input_size;
extern int inbytes;

/* accessed only in playback thread/setup */
static fftwf_plan fftwf_weight;
static float *work;
static float *freq;

static int blocksize=0;
static int lopad=0,hipad=0;
static u_int32_t *widthlookup=0;
static float *window=0;
static float width=.5;
static float **lap=0;
static float **cache;
static int cache_samples;
static int fillstate=0; /* 0: uninitialized
			   1: normal
			   2: eof processed */
static time_linkage out;

/* accessed across threads */
sig_atomic_t declip_active=0;
sig_atomic_t declip_visible=0;
sig_atomic_t declip_converge=2; /* 0=over, 1=full, 2=half, 3=partial, 4=approx */

static float *chtrigger=0;
static sig_atomic_t pending_blocksize=0;
static float convergence=0.;
static float iterations=0.;


/* feedback! */
typedef struct declip_feedback{
  feedback_generic parent_class;
  float *peak;
  int *clipcount;
  int total;
} declip_feedback;

static feedback_generic_pool feedpool;

static feedback_generic *new_declip_feedback(void){
  declip_feedback *ret=malloc(sizeof(*ret));
  ret->clipcount=malloc((input_ch)*sizeof(*ret->clipcount));
  ret->peak=malloc((input_ch)*sizeof(*ret->peak));
  return (feedback_generic *)ret;
}

static void push_declip_feedback(int *clip,float *peak,int total){
  int n=input_ch;
  declip_feedback *f=(declip_feedback *)
    feedback_new(&feedpool,new_declip_feedback);
  memcpy(f->clipcount,clip,n*sizeof(*clip));
  memcpy(f->peak,peak,n*sizeof(*peak));
  f->total=total;
  feedback_push(&feedpool,(feedback_generic *)f);
}

int pull_declip_feedback(int *clip,float *peak,int *total){
  declip_feedback *f=(declip_feedback *)feedback_pull(&feedpool);

  if(!f)return 0;

  if(clip)memcpy(clip,f->clipcount,sizeof(*clip)*input_ch);
  if(peak)memcpy(peak,f->peak,sizeof(*peak)*input_ch);
  if(total)*total=f->total;

  feedback_old(&feedpool,(feedback_generic *)f);
  return 1;
}

/* called only by initial setup */
int declip_load(void){
  int i;
  chtrigger=malloc(input_ch*sizeof(*chtrigger));
  for(i=0;i<input_ch;i++)
    chtrigger[i]=1.;
  
  out.size=input_size;
  out.channels=input_ch;
  out.rate=input_rate;
  out.data=malloc(input_ch*sizeof(*out.data));
  for(i=0;i<input_ch;i++)
    out.data[i]=malloc(input_size*sizeof(**out.data));

  fillstate=0;
  cache=malloc(input_ch*sizeof(*cache));
  for(i=0;i<input_ch;i++)
    cache[i]=malloc(input_size*sizeof(**cache));
  lap=malloc(input_ch*sizeof(*lap));
  for(i=0;i<input_ch;i++)
    lap[i]=malloc(input_size*sizeof(**lap));

  return(0);
}

int declip_setblock(int n){
  if(n<32)return -1;
  if(n>input_size*2)return -1;
  pending_blocksize=n;
  return 0;
}

int declip_settrigger(float trigger,int ch){
  if(ch<0 || ch>=input_ch)return -1;
  pthread_mutex_lock(&master_mutex);
  chtrigger[ch]=trigger-(1./(1<<(inbytes*8-1)))-(1./(1<<(inbytes*8-2)));
  pthread_mutex_unlock(&master_mutex);
  return 0;
}

int declip_setiterations(float it){
  pthread_mutex_lock(&master_mutex);
  iterations=it;
  pthread_mutex_unlock(&master_mutex);
  return 0;
}

int declip_setconvergence(float c){
  pthread_mutex_lock(&master_mutex);
  convergence=c;
  pthread_mutex_unlock(&master_mutex);
  return 0;
}

/* called only in playback thread */
int declip_reset(void){
  /* reset cached pipe state */
  fillstate=0;
  while(pull_declip_feedback(NULL,NULL,NULL));
  return 0;
}

static void sliding_bark_average(float *f,int n,float width){
  int i=0;
  float acc=0.,del=0.;
  float sec[hipad+1];

  memset(sec,0,sizeof(sec));

  for(i=0;i<n/2;i++){

    int hi=widthlookup[i]>>16;
    int lo=widthlookup[i]&(0xffff);
    float del=hypot(f[(i<<1)+1],f[i<<1])/(lo-hi);

    float hidel=del/((i-hi+lopad));
    float lodel=del/((lo-i-lopad));

    sec[hi]+=hidel;
    sec[i+lopad]-=hidel;
    sec[i+lopad]-=lodel;
    sec[lo]+=lodel;

  }

  for(i=0;i<lopad;i++){
    del+=sec[i];
    acc+=del;
  }

  for(i=0;i<n/2;i++){
    f[(i<<1)+1]=f[i<<1]=1./(acc*acc);
    del+=sec[i+lopad];
    acc+=del;
    
  }
  f[n+1]=f[n]=f[n-1];
}

/* work,freq are passed through the static buffer fftwf requires */
static void declip(float *lap,float *out,
		   int blocksize,float trigger,
		   float epsilon, float iteration,
		   int *runningtotal, int *runningcount,float *peak){
  float flag[blocksize*2];
  int    iterbound,i,j,count=0;
  
  for(i=blocksize/2;i<blocksize*3/2;i++){
    if(fabs(work[i])>*peak)*peak=fabs(work[i]);
    flag[i]=0.;
    if(work[i]>=trigger || work[i]<=-trigger){
      flag[i]=1.;
      count++;
    }
  }
  
  *runningtotal+=blocksize;
  *runningcount+=count;

  if(declip_active){

    for(i=0;i<blocksize/2;i++)flag[i]=0.;
    for(i=blocksize*3/2;i<blocksize*2;i++)flag[i]=0.;

    for(i=0;i<blocksize/2;i++)work[i]=0.;
    for(i=0;i<blocksize;i++)work[i+blocksize/2]*=window[i];
    for(i=blocksize*3/2;i<blocksize*2;i++)work[i]=0.;

    fftwf_execute(fftwf_weight);
    sliding_bark_average(freq,blocksize*2,width);
    iterbound=blocksize*iteration;
    if(iterbound<10)iterbound=10;

    if(count)reconstruct(work,freq,flag,epsilon,iterbound);

    if(out)
      for(i=0;i<blocksize/2;i++)
	out[i]=lap[i]+work[i+blocksize/2]*window[i];
    
    for(i=blocksize/2,j=0;i<blocksize;i++)
      lap[j++]=work[i+blocksize/2]*window[i];

  }else{

    if(out)
      for(i=0;i<blocksize/2;i++)
	out[i]=work[i+blocksize/2];
  
    for(i=blocksize/2,j=0;i<blocksize;i++)
      lap[j++]=work[i+blocksize/2]*window[i]*window[i];
  }
  for(i=blocksize/2;i<input_size;i++)
    lap[i]=0.;
}

/* called only by playback thread */
time_linkage *declip_read(time_linkage *in){
  int i;
  float local_trigger[input_ch];
  int total=0;
  int count[input_ch];
  float peak[input_ch];

  time_linkage dummy;

  float local_convergence;
  float local_iterations;
  
  pthread_mutex_lock(&master_mutex);
  local_convergence=convergence;
  local_iterations=iterations;
  memcpy(local_trigger,chtrigger,sizeof(local_trigger));
  pthread_mutex_unlock(&master_mutex);

  memset(count,0,sizeof(count));
  memset(peak,0,sizeof(peak));

  if(pending_blocksize!=blocksize){
    if(blocksize){
      free(widthlookup);
      free(window);
      fftwf_destroy_plan(fftwf_weight);
      fftwf_free(freq);
      fftwf_free(work);
    }
    blocksize=pending_blocksize;

    freq=fftwf_malloc((blocksize*2+2)*sizeof(freq));
    work=fftwf_malloc((blocksize*2)*sizeof(freq));
    fftwf_weight=fftwf_plan_dft_r2c_1d(blocksize*2,
				       work,
				       (fftwf_complex *)freq,
				       FFTW_MEASURE);

    lopad=1-rint(fromBark(toBark(0.)-width)*blocksize*2/input_rate);
    hipad=rint(fromBark(toBark(input_rate*.5)+width)*blocksize*2/input_rate)+lopad;
    widthlookup=malloc((hipad+1)*sizeof(*widthlookup));
    for(i=0;i<blocksize;i++){
      float bark=toBark(input_rate*i/(blocksize*2));
      int hi=rint(fromBark(bark-width)*(blocksize*2)/input_rate)-1+lopad;
      int lo=rint(fromBark(bark+width)*(blocksize*2)/input_rate)+1+lopad;
      widthlookup[i]=(hi<<16)+lo;
    }
    
    window=malloc(blocksize*sizeof(*window));
    for(i=0;i<blocksize;i++) window[i]=sin( M_PIl*i/blocksize );
    for(i=0;i<blocksize;i++) window[i]*=window[i];
    for(i=0;i<blocksize;i++) window[i]=sin(window[i]*M_PIl*.5);

    reconstruct_reinit(blocksize*2);
  }

  switch(fillstate){
  case 0: /* prime the lapping and cache */
    for(i=0;i<input_ch;i++){
      float *temp=in->data[i];
      total=0;
      memset(work+blocksize/2,0,sizeof(*work)*blocksize/2);
      memcpy(work+blocksize,temp,sizeof(*work)*blocksize/2);
      declip(lap[i],0,blocksize,
	     local_trigger[i],local_convergence,local_iterations,
	     &total,count+i,peak+i);
      
      memset(cache[i],0,sizeof(**cache)*input_size);
      in->data[i]=cache[i];
      cache[i]=temp;
    }
    cache_samples=in->samples;
    fillstate=1;
    out.samples=0;
    if(in->samples==in->size)goto tidy_up;
    
    for(i=0;i<input_ch;i++)
      memset(in->data[i],0,sizeof(**in->data)*in->size);
    in->samples=0;
    /* fall through */
  case 1: /* nominal processing */
    for(i=0;i<input_ch;i++){
      float *temp=cache[i];
      int j;
      total=0;
      for(j=0;j+blocksize<=out.size;j+=blocksize/2){
	memcpy(work+blocksize/2,temp+j,sizeof(*work)*blocksize);
	declip(lap[i],out.data[i]+j,blocksize,
	       local_trigger[i],local_convergence,local_iterations,
	       &total,count+i,peak+i);
      }
      memcpy(work+blocksize/2,temp+j,sizeof(*work)*blocksize/2);
      memcpy(work+blocksize,in->data[i],sizeof(*work)*blocksize/2);
      
      declip(lap[i],out.data[i]+j,blocksize,
	     local_trigger[i],local_convergence,local_iterations,
	     &total,count+i,peak+i);
      
      cache[i]=in->data[i];
      in->data[i]=temp;
    }
    out.samples=cache_samples;
    cache_samples=in->samples;
    if(out.samples<out.size)fillstate=2;
    break;
  case 2: /* we've pushed out EOF already */
    out.samples=0;
  }

  push_declip_feedback(count,peak,total);

 tidy_up:
  {
    int tozero=out.size-out.samples;
    if(tozero)
      for(i=0;i<out.channels;i++)
	memset(out.data[i]+out.samples,0,sizeof(**out.data)*tozero);
  }

  return &out;
}
