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
#include "feedback.h"
#include "freq.h"
#include "lpc.h"

extern int input_rate;
extern int input_ch;
extern int input_size;

/* feedback! */
typedef struct freq_feedback{
  feedback_generic parent_class;
  float **peak;
  float **rms;
  int bypass;
} freq_feedback;

static feedback_generic *new_freq_feedback(void){
  freq_feedback *ret=calloc(1,sizeof(*ret));
  return (feedback_generic *)ret;
}

/* total, peak, rms are pulled in array[freqs][input_ch] order */

int pull_freq_feedback(freq_state *ff,float **peak,float **rms){
  freq_feedback *f=(freq_feedback *)feedback_pull(&ff->feedpool);
  int i;
  
  if(!f)return 0;
  
  if(f->bypass){
    feedback_old(&ff->feedpool,(feedback_generic *)f);
    return 2;
  }else{
    if(peak)
      for(i=0;i<ff->fc->bands;i++)
	memcpy(peak[i],f->peak[i],sizeof(**peak)*input_ch);
    if(rms)
      for(i=0;i<ff->fc->bands;i++)
	memcpy(rms[i],f->rms[i],sizeof(**rms)*input_ch);
    feedback_old(&ff->feedpool,(feedback_generic *)f);
    return 1;
  }
}

int freq_class_load(freq_class_setup *f,const float *frequencies, int bands){
  int i,j;
  int blocksize=input_size*2;
  memset(f,0,sizeof(*f));

  f->qblocksize=input_size;
  f->bands=bands;

  /* fill in time window */
  f->window=malloc(blocksize*sizeof(*f->window)); 
  for(i=0;i<blocksize;i++)f->window[i]=sin(M_PIl*i/blocksize);
  for(i=0;i<blocksize;i++)f->window[i]*=f->window[i];
  for(i=0;i<blocksize;i++)f->window[i]=sin(f->window[i]*M_PIl*.5);
  for(i=0;i<blocksize;i++)f->window[i]*=f->window[i];

  f->fftwf_buffer = fftwf_malloc(sizeof(*f->fftwf_buffer) * 
				 (f->qblocksize*4+2));
    
  f->fftwf_forward=fftwf_plan_dft_r2c_1d(f->qblocksize*4, 
					 f->fftwf_buffer,
					 (fftwf_complex *)(f->fftwf_buffer), 
					 FFTW_MEASURE);
  
  f->fftwf_backward=fftwf_plan_dft_c2r_1d(f->qblocksize*4,
					  (fftwf_complex *)(f->fftwf_buffer), 
					  f->fftwf_buffer, 
					  FFTW_MEASURE);

  /* unlike old postfish, we offer all frequencies via smoothly
     supersampling the spectrum */
  /* I'm too lazy to figure out the integral symbolically, use this
     fancy CPU thingy for something */
  
  f->ho_window=malloc(bands*sizeof(*f->ho_window));
  f->ho_area=calloc(bands,sizeof(*f->ho_area));
  {
    float working[f->qblocksize*4+2];
    
    for(i=0;i<bands;i++){
      float lastf=(i>0?frequencies[i-1]:0);
      float thisf=frequencies[i];
      float nextf=frequencies[i+1];
      memset(working,0,sizeof(working));

      for(j=0;j<((f->qblocksize*2+1)<<5);j++){
        float localf= .5*j*input_rate/(f->qblocksize<<6);
        int localbin= j>>5;
        float localwin;

        if(localf>=lastf && localf<thisf){
          if(i==0)
            localwin=1.;
          else
            localwin= sin((localf-lastf)/(thisf-lastf)*M_PIl*.5);
          localwin*=localwin;
	  working[localbin]+=localwin*(1./32);

        }else if(localf>=thisf && localf<nextf){
          if(i+1==bands)
            localwin=1.;
          else
            localwin= sin((nextf-localf)/(nextf-thisf)*M_PIl*.5);
          
          localwin*=localwin;
          working[localbin]+=localwin*(1./32);
          
        }
      }

      /* window this desired response in the time domain so that our
         convolution is properly padded against being circular */
      memset(f->fftwf_buffer,0,sizeof(*f->fftwf_buffer)*
	     (f->qblocksize*4+2));
      for(j=0;j<f->qblocksize*2;j++)
	f->fftwf_buffer[j*2]=working[j];
      
      fftwf_execute(f->fftwf_backward);

      /* window response in time */
      for(j=0;j<f->qblocksize;j++){
	float val=cos(j*M_PI/(f->qblocksize*2));
	val=sin(val*val*M_PIl*.5);
	f->fftwf_buffer[j]*= sin(val*val*M_PIl*.5);
      }

      for(;j<f->qblocksize*3;j++)
	f->fftwf_buffer[j]=0.;
      
      for(;j<f->qblocksize*4;j++){
	float val=sin((j-f->qblocksize*3)*M_PI/(f->qblocksize*2));
	val=sin(val*val*M_PIl*.5);
	f->fftwf_buffer[j]*=sin(val*val*M_PIl*.5);
      }

      /* back to frequency; this is all-real data still */
      fftwf_execute(f->fftwf_forward);
      for(j=0;j<f->qblocksize*4+2;j++)
	f->fftwf_buffer[j]/=f->qblocksize*4;
      
      /* now take what we learned and distill it a bit */
      f->ho_window[i]=calloc((f->qblocksize*2+1),sizeof(**f->ho_window));
      for(j=0;j<f->qblocksize*2+1;j++){
	f->ho_window[i][j]=f->fftwf_buffer[j*2];
	f->ho_area[i]+=fabs(f->ho_window[i][j]);
      }
      f->ho_area[i]=1./f->ho_area[i];

      lastf=thisf;
      
    }
  }

  return 0;

}

/* called only by initial setup */
int freq_load(freq_state *f,freq_class_setup *fc){
  int i;
  memset(f,0,sizeof(*f));

  f->fc=fc;

  f->fillstate=0;
  f->cache_samples=0;
  f->cache1=malloc(input_ch*sizeof(*f->cache1));
  f->cache0=malloc(input_ch*sizeof(*f->cache0));
  for(i=0;i<input_ch;i++){
    f->cache1[i]=calloc(input_size,sizeof(**f->cache1));
    f->cache0[i]=calloc(input_size,sizeof(**f->cache0));
  }

  f->activeP=malloc(input_ch*sizeof(*f->activeP));
  f->active1=malloc(input_ch*sizeof(*f->active1));
  f->active0=malloc(input_ch*sizeof(*f->active0));


  f->lap1=malloc(input_ch*sizeof(*f->lap1));
  f->lap0=malloc(input_ch*sizeof(*f->lap0));
  f->lapC=malloc(input_ch*sizeof(*f->lapC));
  for(i=0;i<input_ch;i++){
    f->lap1[i]=calloc(fc->qblocksize,sizeof(**f->lap1));
    f->lap0[i]=calloc(fc->qblocksize,sizeof(**f->lap0));
    f->lapC[i]=calloc(fc->qblocksize,sizeof(**f->lapC));
  }

  f->peak=malloc(fc->bands*sizeof(*f->peak));
  f->rms=malloc(fc->bands*sizeof(*f->rms));
  for(i=0;i<fc->bands;i++){
    f->peak[i]=malloc(input_ch*sizeof(**f->peak));
    f->rms[i]=malloc(input_ch*sizeof(**f->rms));
  }

  f->out.size=input_size;
  f->out.channels=input_ch;
  f->out.rate=input_rate;
  f->out.data=malloc(input_ch*sizeof(*f->out.data));
  for(i=0;i<input_ch;i++)
    f->out.data[i]=malloc(input_size*sizeof(**f->out.data));
  
  return(0);
}

/* called only in playback thread */
int freq_reset(freq_state *f){
  /* reset cached pipe state */
  f->fillstate=0;
  while(pull_freq_feedback(f,NULL,NULL));
  return 0;
}

static void freq_metric_work(float *x,freq_class_setup *c,
			     float **peak,float **rms,int channel){
  int i,j;
  float sq_mags[c->qblocksize*2+1];
  
  for(i=0;i<c->qblocksize*2+1;i++)
    sq_mags[i]=(x[i*2]*x[i*2]+x[i*2+1]*x[i*2+1])*64.;
  
  for(i=0;i<c->bands;i++){
    float *ho_window=c->ho_window[i];
    float lrms=0.;
    float lpeak=0.;
    for(j=0;j<c->qblocksize*2+1;j++){
      float val=fabs(sq_mags[j]*ho_window[j]);
      lrms+=val*.5;
      if(val>lpeak)lpeak=val;
    }
    rms[i][channel]=todB(lrms*c->ho_area[i])*.5;
    peak[i][channel]=todB(lpeak)*.5;
  }
}

static void fill_freq_buffer_helper(float *buffer,float *window,
				    float *cache, float *in,
				    int qblocksize,int muted0,int mutedC,
				    float scale){
  int i;
  
  /* place data in fftwf buffer */
  memset(buffer,0,sizeof(*buffer)*qblocksize);

  if(muted0)
    memset(buffer+qblocksize,0,sizeof(*buffer)*qblocksize);
  else
    memcpy(buffer+qblocksize,cache,sizeof(*buffer)*qblocksize);

  if(mutedC)
    memset(buffer+qblocksize*2,0,sizeof(*buffer)*qblocksize);
  else
    memcpy(buffer+qblocksize*2,in,sizeof(*buffer)*qblocksize);
  
  memset(buffer+qblocksize*3,0,sizeof(*buffer)*qblocksize);

  /* window (if nonzero) */
  if(!muted0 || !mutedC){
    buffer+=qblocksize;
    for(i=0;i<qblocksize*2;i++)
      buffer[i] *= window[i]*scale;
  }
}

static void freq_work(freq_class_setup *fc,
		      freq_state *f,
		      time_linkage *in,
		      time_linkage *out,
		      int      *visible,
		      int      *active,
		      void (*func)(float *,int)){
  
  int i,j;
  int have_feedback=0;
  
  f->cache_samples+=in->samples;

  for(i=0;i<input_ch;i++){
    int mutedC=mute_channel_muted(in->active,i);
    int muted0=mute_channel_muted(f->mutemask0,i);

    int activeC=active[i] && !(muted0 && mutedC);
    int active0=f->active0[i];
    int active1=f->active1[i];
    int activeP=f->activeP[i];
    
    /* zero the feedback */
    for(j=0;j<fc->bands;j++){
      f->peak[j][i]=-150.;
      f->rms[j][i]=-150.;
    }
    
    /* processing pathway depends on active|visible.  If we're visible & !active, the transform 
       data is thrown away without being manipulated or used in output (it is merely measured) */
    int trans_active= (visible[i] || active[i]) && !(muted0 && mutedC);

    if(trans_active){
      
      /* pre- and post-extrapolate to avoid harsh edge features.
         Account for muting in previous or current frame */
      if(f->fillstate==0){
	if(mutedC)memset(in->data[i],0,sizeof(**in->data)*input_size);
	if(muted0)memset(f->cache0[i],0,sizeof(**f->cache0)*input_size);
	preextrapolate_helper(in->data[i],input_size,
			      f->cache0[i],input_size);
      }
    
      if(in->samples<in->size){
	if(mutedC)memset(in->data[i],0,sizeof(**in->data)*input_size);
	if(muted0)memset(f->cache0[i],0,sizeof(**f->cache0)*input_size);

	postextrapolate_helper(f->cache0[i],input_size,
			       in->data[i],in->samples,
			       in->data[i]+in->samples,
			       in->size-in->samples);
      }

      fill_freq_buffer_helper(fc->fftwf_buffer,
			      fc->window,
			      f->cache0[i],in->data[i],
			      fc->qblocksize,muted0,mutedC,
			      .25/fc->qblocksize);

      /* transform the time data */
      fftwf_execute(fc->fftwf_forward);

      if(activeC)func(fc->fftwf_buffer,i);
      
      /* feedback and reverse transform */
      have_feedback=1;
      freq_metric_work(fc->fftwf_buffer,fc,f->peak,f->rms,i);
      if(activeC)
	fftwf_execute(fc->fftwf_backward);
      else
	trans_active=0;

    }else{
      if(visible[i]){
	/* push zeroed feedback */
	have_feedback=1;
      } /* else bypass feedback */
    }

    /* output data pathway depends on activity over the past four
       frames (including this one); draw from transform (if any) and
       lap, cache and lap, or just cache? */
    if(!activeP && !active1 && !active0 && !activeC){
      /* bypass; rotate the cache */
      
      if(out){
	float *temp=out->data[i];
	out->data[i]=f->cache1[i];
	f->cache1[i]=temp;
      }
    }else{
      float *w2=fc->window+input_size;
      float *l0=f->lap0[i];
      float *l1=f->lap1[i];
      float *lC=f->lapC[i];
      float *c0=f->cache0[i];
      float *c1=f->cache1[i];

      float *t1=fc->fftwf_buffer;
      float *t0=t1+input_size;
      float *tC=t0+input_size;
      float *tN=tC+input_size;

      if(!trans_active){
	/* lap the cache into the trasform vector */
	fill_freq_buffer_helper(fc->fftwf_buffer,
				fc->window,
				f->cache0[i],in->data[i],
				fc->qblocksize,muted0,mutedC,1.);
      }

      if(!activeP && !active1 && !active0){
	/* previously in a bypassed/inactive state; the lapping cache
           will need to be re-prepared */
	
	memcpy(l1,c1,sizeof(*l1)*input_size);
	for(j=0;j<input_size;j++)
	  l0[j]= c0[j]*w2[j];
	memset(lC,0,sizeof(*lC)*input_size);
	
      }

      if(out){
	float *ox=out->data[i];
	
	for(j=0;j<input_size;j++)
	  ox[j]= l1[j]+t1[j];
      }

      for(j=0;j<input_size;j++){
	l1[j]= l0[j]+t0[j];
	l0[j]= lC[j]+tC[j];
	lC[j]= tN[j];
      }
    }

    f->activeP[i]=active1;
    f->active1[i]=active0;
    f->active0[i]=activeC;
    
    {
      float *temp=f->cache1[i];
      f->cache1[i]=f->cache0[i];
      f->cache0[i]=in->data[i];
      in->data[i]=temp;
    }
  }

  /* log feedback metrics; this logs one for every call, rather than
   every output; that's fine; nothing flushes until first output, and
   any playback halt will kill off stragglers.  Sync is unaffected. */
  if(have_feedback){
    freq_feedback *ff=
      (freq_feedback *)feedback_new(&f->feedpool,new_freq_feedback);
    
    if(!ff->peak){
      ff->peak=calloc(fc->bands,sizeof(*ff->peak));
      for(i=0;i<fc->bands;i++)
	ff->peak[i]=malloc(input_ch*sizeof(**ff->peak));
    }
    if(!ff->rms){
      ff->rms=calloc(fc->bands,sizeof(*ff->rms));
      for(i=0;i<fc->bands;i++)
	ff->rms[i]=malloc(input_ch*sizeof(**ff->rms));
    }

    for(i=0;i<fc->bands;i++){
      memcpy(ff->peak[i],f->peak[i],input_ch*sizeof(**f->peak));
      memcpy(ff->rms[i],f->rms[i],input_ch*sizeof(**f->rms));
    } 
    ff->bypass=0;
    feedback_push(&f->feedpool,(feedback_generic *)ff);
  }else{
    freq_feedback *ff=
      (freq_feedback *)feedback_new(&f->feedpool,new_freq_feedback);
    ff->bypass=1;
    feedback_push(&f->feedpool,(feedback_generic *)ff);
  }

  /* complete output linkage */
  if(out){
    out->active=f->mutemask1;
    f->out.samples=(f->cache_samples>input_size?input_size:f->cache_samples);
    f->cache_samples-=f->out.samples;
  }

  f->mutemaskP=f->mutemask1;
  f->mutemask1=f->mutemask0;
  f->mutemask0=in->active;
}

/* called only by playback thread */
time_linkage *freq_read(time_linkage *in, freq_state *f,
			int *visible, int *active,
			void (*func)(float *,int i)){
  int i;
  freq_class_setup *fc=f->fc;

  switch(f->fillstate){
  case 0: /* prime the lapping and cache */
    if(in->samples==0){
      f->out.samples=0;
      return &f->out;
    }

    /* zero out lapping and cache state */
    for(i=0;i<input_ch;i++){
      memset(f->lap1[i],0,sizeof(**f->lap1)*fc->qblocksize);
      memset(f->lap0[i],0,sizeof(**f->lap0)*fc->qblocksize);
      memset(f->cache0[i],0,sizeof(**f->cache0)*input_size);
      memset(f->cache1[i],0,sizeof(**f->cache1)*input_size);
      f->activeP[i]=active[i];
      f->active1[i]=active[i];
      f->active0[i]=active[i];
    }

    f->cache_samples=0;
    f->mutemask0=f->mutemask1=f->mutemaskP=in->active;
    
    freq_work(fc,f,in,0,visible,active,func);

    f->fillstate=1;
    f->out.samples=0;
    if(in->samples==in->size)goto tidy_up;
    
    for(i=0;i<input_ch;i++)
      memset(in->data[i],0,sizeof(**in->data)*in->size);
    in->samples=0;
    /* fall through */
    
  case 1: /* finish priming the lapping and cache */

    freq_work(fc,f,in,0,visible,active,func);
    
    f->fillstate=2;
    f->out.samples=0;
    if(in->samples==in->size)goto tidy_up;
    
    for(i=0;i<input_ch;i++)
      memset(in->data[i],0,sizeof(**in->data)*in->size);
    in->samples=0;
    /* fall through */
    
  case 2: /* nominal processing */

    freq_work(fc,f,in,&f->out,visible,active,func);

    if(f->out.samples<f->out.size)f->fillstate=3;
    break;
  case 3: /* we've pushed out EOF already */
    f->out.samples=0;
  }
  
 tidy_up:
  {
    int tozero=f->out.size-f->out.samples;
    if(tozero)
      for(i=0;i<f->out.channels;i++)
	memset(f->out.data[i]+f->out.samples,0,sizeof(**f->out.data)*tozero);
  }
  
  return &f->out;
}
