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
#include <math.h>
#include <sys/types.h>
#include <fftw3.h>
#include "reconstruct.h"
#include "window.h"
#include "feedback.h"

extern int input_rate;
extern int input_ch;
extern int input_size;

/* accessed only in playback thread/setup */

static fftwf_plan fftwf_weight;

static float *work;
static float *freq;

static int blocksize=0;
static int lopad=0,hipad=0;
static u_int32_t *widthlookup=0;

static float *leftwindow=0;
static float *rightwindow=0;
static int left=0;
static int right=0;
static float width=.5;
static float **lap;
static float **cache;
static u_int32_t cache_active;
static int cache_samples;
static int fillstate=0; /* 0: uninitialized
			   1: normal
			   2: eof processed */
static time_linkage out;

/* accessed across threads */
sig_atomic_t *declip_active;
int *declip_prev_active;

sig_atomic_t declip_visible=0;

sig_atomic_t *declip_chtrigger=0;
sig_atomic_t declip_pending_blocksize=0;
sig_atomic_t declip_convergence=0;
sig_atomic_t declip_iterations=0;

/* feedback! */
typedef struct declip_feedback{
  feedback_generic parent_class;
  float *peak;
  int   *clipcount;
  int   *total;
} declip_feedback;

static feedback_generic_pool feedpool;

static feedback_generic *new_declip_feedback(void){
  declip_feedback *ret=malloc(sizeof(*ret));
  ret->clipcount=malloc((input_ch)*sizeof(*ret->clipcount));
  ret->peak=malloc((input_ch)*sizeof(*ret->peak));
  ret->total=malloc((input_ch)*sizeof(*ret->total));
  return (feedback_generic *)ret;
}

static void push_declip_feedback(int *clip,float *peak,int *total){
  int n=input_ch;
  declip_feedback *f=(declip_feedback *)
    feedback_new(&feedpool,new_declip_feedback);
  memcpy(f->clipcount,clip,n*sizeof(*clip));
  memcpy(f->peak,peak,n*sizeof(*peak));
  memcpy(f->total,total,n*sizeof(*total));
  feedback_push(&feedpool,(feedback_generic *)f);
}

int pull_declip_feedback(int *clip,float *peak,int *total){
  declip_feedback *f=(declip_feedback *)feedback_pull(&feedpool);

  if(!f)return 0;

  if(clip)memcpy(clip,f->clipcount,sizeof(*clip)*input_ch);
  if(peak)memcpy(peak,f->peak,sizeof(*peak)*input_ch);
  if(total)memcpy(total,f->total,sizeof(*total)*input_ch);

  feedback_old(&feedpool,(feedback_generic *)f);
  return 1;
}

static void apply_window(float *out,float *data,int sq){
  int i,j;
  int half=max(left,right);

  for(i=0;i<half-left;i++)
    out[i]=0;

  if(sq){
    for(j=0;i<half;i++,j++)
      out[i]=data[i]*leftwindow[j]*leftwindow[j];
    for(j=0;j<right;i++,j++)
      out[i]=data[i]*rightwindow[right-j]*rightwindow[right-j];
  }else{
    for(j=0;i<half;i++,j++)
      out[i]=data[i]*leftwindow[j];
    for(j=0;j<right;i++,j++)
      out[i]=data[i]*rightwindow[right-j];
  }

  for(;i<half*2;i++)
    out[i]=0;
}

static void setup_window(int lleft,int lright){
  left=lleft;
  right=lright;

  leftwindow=window_get(2,left);
  rightwindow=window_get(2,right);
}

static void setup_blocksize(int newblocksize){
  int i;

  if(blocksize)fftwf_destroy_plan(fftwf_weight);
  blocksize=newblocksize;
  fftwf_weight=fftwf_plan_dft_r2c_1d(blocksize*2,work,
					(fftwf_complex *)freq,
					FFTW_MEASURE);
  
  lopad=1-rint(fromBark(toBark(0.)-width)*blocksize*2/input_rate);
  hipad=rint(fromBark(toBark(input_rate*.5)+width)*blocksize*2/input_rate)+lopad;
  for(i=0;i<blocksize;i++){
    float bark=toBark(input_rate*i/(blocksize*2));
    int hi=rint(fromBark(bark-width)*(blocksize*2)/input_rate)-1+lopad;
    int lo=rint(fromBark(bark+width)*(blocksize*2)/input_rate)+1+lopad;
    widthlookup[i]=(hi<<16)+lo;
  }

  reconstruct_reinit(blocksize*2);
}

/* called only by initial setup */
int declip_load(void){
  int i,j;
  declip_active=calloc(input_ch,sizeof(*declip_active));
  declip_prev_active=calloc(input_ch,sizeof(*declip_prev_active));
  declip_chtrigger=malloc(input_ch*sizeof(*declip_chtrigger));
  for(i=0;i<input_ch;i++)
    declip_chtrigger[i]=10000;
  
  out.channels=input_ch;
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
  
  {    
    /* alloc for largest possible blocksize */
    int blocksize=input_size*2;
    int loestpad=1-rint(fromBark(toBark(0.)-width)*blocksize*2/input_rate);
    int hiestpad=rint(fromBark(toBark(input_rate*.5)+width)*blocksize*2/input_rate)+loestpad;
    widthlookup=malloc((hiestpad+1)*sizeof(*widthlookup));
    freq=fftwf_malloc((blocksize*2+2)*sizeof(*freq));
    work=fftwf_malloc((blocksize*2)*sizeof(*work));

    for(i=0,j=32;j<=blocksize*2;i++,j*=2){
      fftwf_weight=fftwf_plan_dft_r2c_1d(j,work,
					 (fftwf_complex *)freq,
					 FFTW_MEASURE);
      fftwf_destroy_plan(fftwf_weight);
    }
  }
  reconstruct_init(32,input_size*4);
  
  declip_pending_blocksize=input_size*2;
  return(0);
}

/* called only in playback thread */
int declip_reset(void){
  /* reset cached pipe state */
  fillstate=0;
  while(pull_declip_feedback(NULL,NULL,NULL));
  return 0;
}

int noisy=0;
static void sliding_bark_average(float *f,int n,float width){
  int i=0;
  double acc=0.,del=0.;
  double sec[hipad+1];

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
static void declip(int blocksize,float trigger,
		   float epsilon, float iteration,
		   int *runningtotal, int *runningcount){
  float flag[blocksize*2];
  int    iterbound,i,count=0;

  /* too many apps screw up proper output scaling, so previously
     clipped audio ends up getting rounded to just short of the rail
     upon export.  To avoid users accidentally shooting themselves in
     the foot, trigger clipping at -.05dB rather than 0dB even if 0dB
     is selected. This corresponds to mis-rounding 8 bit audio by 1.5
     steps.*/
  if(trigger>.99426)trigger=.99426; 

  for(i=blocksize/2;i<blocksize*3/2;i++){
    flag[i]=0.;
    if(work[i]>=trigger || work[i]<=-trigger){
      flag[i]=1.;
      count++;
    }
  }

  
  *runningtotal+=blocksize;
  *runningcount+=count;

  if(count){
    for(i=0;i<blocksize/2;i++)flag[i]=0.;
    for(i=blocksize*3/2;i<blocksize*2;i++)flag[i]=0.;
    apply_window(work+blocksize/2,work+blocksize/2,0);
    
    fftwf_execute(fftwf_weight);
    sliding_bark_average(freq,blocksize*2,width);
    iterbound=blocksize*iteration;
    if(iterbound<10)iterbound=10;
    
    reconstruct(work,freq,flag,epsilon,iterbound);

    apply_window(work+blocksize/2,work+blocksize/2,0);
  }else
    apply_window(work+blocksize/2,work+blocksize/2,1);

}

/* called only by playback thread */
time_linkage *declip_read(time_linkage *in){
  int i,j,k;
  float local_trigger[input_ch];
  int count[input_ch];
  int total[input_ch];
  float peak[input_ch];
  u_int32_t active=0;
  int next_blocksize=declip_pending_blocksize;
  int orig_blocksize;

  float local_convergence;
  float local_iterations;
  
  for(i=0;i<input_ch;i++)
    local_trigger[i]=declip_chtrigger[i]*.0001;
  local_iterations=declip_iterations*.0001;
  local_convergence=fromdB(declip_convergence*.1);

  memset(count,0,sizeof(count));
  memset(peak,0,sizeof(peak));
  memset(total,0,sizeof(total));

  switch(fillstate){
  case 0: /* prime the lapping and cache */

    /* set up for the blocksize we're actually using for now */
    {
      setup_blocksize(next_blocksize);
      setup_window(blocksize/2,blocksize/2);
    }

    for(i=0;i<input_ch;i++){
      int channel_active=declip_active[i]; 
      declip_prev_active[i]=channel_active;

      /* peak feedback */
      if(declip_visible && !mute_channel_muted(in->active,i)){
	float *l=in->data[i];
	for(j=0;j<in->samples;j++)
	  if(fabs(l[j])>peak[i])peak[i]=fabs(l[j]);
      }

      if(channel_active){
	
	/* fill work with the block spanning cache/in (first 1/4, last 1/4 are zeroed) */
	memset(work,0,sizeof(*work)*blocksize);
	memcpy(work+blocksize,in->data[i],sizeof(*work)*blocksize/2);
	memset(work+blocksize+blocksize/2,0,sizeof(*work)*blocksize/2);
	declip(blocksize,local_trigger[i],local_convergence,local_iterations,
	       total+i,count+i);

	/* second half of work goes to lap */
	memcpy(lap[i],work+blocksize,sizeof(*work)*blocksize/2);

	/* now iterate the pieces purely within in */
	for(j=0;j+blocksize<=input_size;j+=blocksize/2){
	  memset(work,0,sizeof(*work)*blocksize);
	  memcpy(work+blocksize/2,in->data[i]+j,sizeof(*work)*blocksize);
	  memset(work+blocksize+blocksize/2,0,sizeof(*work)*blocksize/2);

	  declip(blocksize,local_trigger[i],local_convergence,local_iterations,
		 total+i,count+i);

	  /* second half of work goes to lap */
	  {
	    float *llap=lap[i]+j;
	    float *lwork=work+blocksize/2;
	    for(k=0;k<blocksize/2;k++)
	      llap[k]+=lwork[k];
	    memcpy(llap+k,lwork+k,sizeof(*work)*blocksize/2);
	  }
	}

      }//else no declipping to do, so direct cache/lap buffer rotation */

      {
	float *temp=cache[i];
	cache[i]=in->data[i];
	in->data[i]=temp;
	memset(temp,0,sizeof(*temp)*input_size);
      }
    }
    cache_samples=in->samples;
    cache_active=in->active;
    fillstate=1;
    out.samples=0;
    if(in->samples==input_size)break;
    
    for(i=0;i<input_ch;i++)
      memset(in->data[i],0,sizeof(**in->data)*input_size);
    in->samples=0;
    /* fall through */

  case 1: /* nominal processing */
    orig_blocksize=blocksize;

    /* the 'gap' transition and finishing off the output block is done
       first as it may need to handle a blocksize transition (and a
       temporary transition window */

    if(next_blocksize != orig_blocksize){
      if(next_blocksize > orig_blocksize) setup_blocksize(next_blocksize);
      setup_window(orig_blocksize/2,next_blocksize/2);
    }

    /* the gap piece is also special in that it may need to deal with
       a transition to/from bypass */
    
    for(i=0;i<input_ch;i++){
      int channel_active=declip_active[i];

      /* peak feedback */
      if(declip_visible && !mute_channel_muted(in->active,i)){
	float *l=in->data[i];
	for(j=0;j<in->samples;j++)
	  if(fabs(l[j])>peak[i])peak[i]=fabs(l[j]);
      }


      if(mute_channel_muted(cache_active,i)){
	
	/* we may need cache for a later transition, so keep it up to date */
	float *temp=cache[i];
	cache[i]=in->data[i];
	in->data[i]=temp;
	
	/* zero the lap */
	if(!mute_channel_muted(in->active,i))
	  memset(lap[i],0,sizeof(*lap[i])*input_size);
	
      }else{
	
	active|=(1<<i); /* audible output in out.data[i] */
	
	if(mute_channel_muted(in->active,i)){
	  if(declip_prev_active[i]){
	    /* Cache: Muted=False, Bypass=False
	       Input: Muted=True,  Bypass=X     */
	    
	    /* transition to mute, so lap is finished output.  Rotate all */
	    float *temp=cache[i];
	    cache[i]=in->data[i];
	    in->data[i]=temp;
	    
	    temp=out.data[i];
	    out.data[i]=lap[i];
	    lap[i]=temp;
	  }else{
	    /* Cache: Muted=False, Bypass=True
	       Input: Muted=True,  Bypass=X     */
	    
	    /* rotate in/cache/out, transition out */
	    float *temp=out.data[i];
	    out.data[i]=cache[i];
	    cache[i]=in->data[i];
	    in->data[i]=temp;
	    
	  }
	}else{
	  if(!declip_prev_active[i]){
	    if(!channel_active){
	      /* Cache: Muted=False, Bypass=True
		 Input: Muted=False, Bypass=True     */
	      
	      /* all bypass! rotate in/cache/out */
	      float *temp=out.data[i];
	      out.data[i]=cache[i];
	      cache[i]=in->data[i];
	      in->data[i]=temp;
	    }else{
	      /* Cache: Muted=False, Bypass=True
		 Input: Muted=False, Bypass=False     */
	      
	      /* transition the lap; right window to left of in */
	      for(j=0;j<right;j++)
		lap[i][j]=in->data[i][j]*
		  rightwindow[right-j]*rightwindow[right-j];
	      
	      /* all rotate in/cache/out */
	      float *temp=out.data[i];
	      out.data[i]=cache[i];
	      cache[i]=in->data[i];
	      in->data[i]=temp;
	    }
	  }else{
	    if(!channel_active){
	      /* Cache: Muted=False, Bypass=False
		 Input: Muted=False, Bypass=True     */
	      
	      /* finish off lap, then rotate all */
	      /* left window to end of cache */
	      for(j=input_size-left,k=0;j<input_size;j++,k++)
		lap[i][j]+=cache[i][j]*leftwindow[k]*leftwindow[k];
	      float *temp=cache[i];
	      cache[i]=in->data[i];
	      in->data[i]=temp;
	      
	      temp=out.data[i];
	      out.data[i]=lap[i];
	      lap[i]=temp;
	    }else{
	      /* Cache: Muted=False, Bypass=False
		 Input: Muted=False, Bypass=False */
	      
	      /* nominal case; the only one involving declipping the gap */
	      memset(work,0,sizeof(*work)*blocksize/2);
	      memcpy(work+blocksize/2,cache[i]+input_size-blocksize/2,sizeof(*work)*blocksize/2);
	      memcpy(work+blocksize,in->data[i],sizeof(*work)*blocksize/2);
	      memset(work+blocksize+blocksize/2,0,sizeof(*work)*blocksize/2);
	      
	      declip(blocksize,local_trigger[i],local_convergence,local_iterations,
		     total+i,count+i);
	      
	      /* finish lap from last frame */
	      {
		float *llap=lap[i]+input_size-blocksize/2;
		float *lwork=work+blocksize/2;
		for(j=0;j<blocksize/2;j++)
		  llap[j]+=lwork[j];
	      }
	      /* rotate buffers */
	      float *temp=out.data[i];
	      out.data[i]=lap[i];
	      lap[i]=temp;
	      
	      temp=in->data[i];
	      in->data[i]=cache[i];
	      cache[i]=temp;
	      
	      /* begin lap for this frame */
	      memcpy(lap[i],work+blocksize,sizeof(*work)*blocksize/2);
	    }
	  }
	}
      }

      declip_prev_active[i]=channel_active;
    }
    
    /* also rotate metadata */
    out.samples=cache_samples;
    cache_samples=in->samples;
    cache_active=in->active;
    
    /* finish transition to new blocksize (if a change is in progress) */
    if(next_blocksize != orig_blocksize){
      if(next_blocksize <= orig_blocksize) setup_blocksize(next_blocksize);
      setup_window(blocksize/2,blocksize/2);
    }

    /* declip the rest of the current frame */
    for(i=0;i<input_ch;i++){
      if(!mute_channel_muted(cache_active,i)){
	/* declip */
	if(declip_prev_active[i]){
	  
	  for(j=0;j+blocksize<=input_size;j+=blocksize/2){
	    memset(work,0,sizeof(*work)*blocksize);
	    memcpy(work+blocksize/2,cache[i]+j,sizeof(*work)*blocksize);
	    memset(work+blocksize+blocksize/2,0,sizeof(*work)*blocksize/2);
	    declip(blocksize,local_trigger[i],local_convergence,local_iterations,
		   total+i,count+i);
	    
	    {
	      float *llap=lap[i]+j;
	      float *lwork=work+blocksize/2;
	      for(k=0;k<blocksize/2;k++)
		llap[k]+=lwork[k];
	      memcpy(llap+k,lwork+k,sizeof(*work)*blocksize/2);
	    }
	  }
	}
      }
    }
    if(out.samples<input_size)fillstate=2;
    break;
  case 2: /* we've pushed out EOF already */
    out.samples=0;
  }

  push_declip_feedback(count,peak,total); /* we can push one (and
                                             exactly one) for every
                                             block that comes in *or*
                                             one for every block that
                                             goes out.  In declip,
                                             it's for every block that
                                             comes in */

  {
    int tozero=input_size-out.samples;
    if(tozero)
      for(i=0;i<out.channels;i++)
	memset(out.data[i]+out.samples,0,sizeof(**out.data)*tozero);
  }

  out.active=active;
  return &out;
}
