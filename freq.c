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
  freq_feedback *ret=malloc(sizeof(*ret));
  ret->peak=calloc(input_ch,sizeof(*ret->peak));
  ret->rms=calloc(input_ch,sizeof(*ret->rms));

  return (feedback_generic *)ret;
}

/* total, peak, rms are pulled in array[freqs][input_ch] order */

int pull_freq_feedback(freq_state *ff,float **peak,float **rms){
  freq_feedback *f=(freq_feedback *)feedback_pull(&ff->feedpool);
  int i,j;
  
  if(!f)return 0;
  
  if(f->bypass){
    feedback_old(&ff->feedpool,(feedback_generic *)f);
    return 2;
  }else{
    if(peak)
      for(i=0;i<ff->bands;i++)
	for(j=0;j<input_ch;j++)
	  peak[i][j]=f->peak[j][i];
    
    if(rms)
      for(i=0;i<ff->bands;i++)
	for(j=0;j<input_ch;j++)
	  rms[i][j]=f->rms[j][i];
    feedback_old(&ff->feedpool,(feedback_generic *)f);
    return 1;
  }
}

/* called only by initial setup */
int freq_load(freq_state *f,const float *frequencies, int bands){
  int i,j;
  int blocksize=input_size*2;
  memset(f,0,sizeof(*f));

  f->qblocksize=input_size;
  f->bands=bands;

  f->fillstate=0;
  f->cache_samples=0;
  f->cache=malloc(input_ch*sizeof(*f->cache));
  for(i=0;i<input_ch;i++)
    f->cache[i]=calloc(input_size,sizeof(**f->cache));
  f->lap=malloc(input_ch*sizeof(*f->lap));
  for(i=0;i<input_ch;i++)
    f->lap[i]=calloc(f->qblocksize*3,sizeof(**f->lap));

  f->out.size=input_size;
  f->out.channels=input_ch;
  f->out.rate=input_rate;
  f->out.data=malloc(input_ch*sizeof(*f->out.data));
  for(i=0;i<input_ch;i++)
    f->out.data[i]=malloc(input_size*sizeof(**f->out.data));

  /* fill in time window */
  f->window=malloc(blocksize*sizeof(*f->window)); 
  for(i=0;i<blocksize;i++)f->window[i]=sin(M_PIl*i/blocksize);
  for(i=0;i<blocksize;i++)f->window[i]*=f->window[i];
  for(i=0;i<blocksize;i++)f->window[i]=sin(f->window[i]*M_PIl*.5);
  for(i=0;i<blocksize;i++)f->window[i]*=f->window[i];

  f->fftwf_buffer = fftwf_malloc(sizeof(*f->fftwf_buffer) * input_ch);
  f->fftwf_backward = fftwf_malloc(sizeof(*f->fftwf_backward) * input_ch);
  f->fftwf_forward = fftwf_malloc(sizeof(*f->fftwf_forward) * input_ch);
  for(i=0;i<input_ch;i++){
    f->fftwf_buffer[i] = fftwf_malloc(sizeof(**f->fftwf_buffer) * 
				  (f->qblocksize*4+2));
    
    f->fftwf_forward[i]=fftwf_plan_dft_r2c_1d(f->qblocksize*4, 
					      f->fftwf_buffer[i],
					      (fftwf_complex *)(f->fftwf_buffer[i]), 
					      FFTW_MEASURE);
    
    f->fftwf_backward[i]=fftwf_plan_dft_c2r_1d(f->qblocksize*4,
					       (fftwf_complex *)(f->fftwf_buffer[i]), 
					       f->fftwf_buffer[i], 
					       FFTW_MEASURE);
  }

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
      memset(f->fftwf_buffer[0],0,sizeof(**f->fftwf_buffer)*
	     (f->qblocksize*4+2));
      for(j=0;j<f->qblocksize*2;j++)
	f->fftwf_buffer[0][j*2]=working[j];
      
      fftwf_execute(f->fftwf_backward[0]);

      /* window response in time */
      for(j=0;j<f->qblocksize;j++){
	float val=cos(j*M_PI/(f->qblocksize*2));
	val=sin(val*val*M_PIl*.5);
	f->fftwf_buffer[0][j]*= sin(val*val*M_PIl*.5);
      }

      for(;j<f->qblocksize*3;j++)
	f->fftwf_buffer[0][j]=0.;
      
      for(;j<f->qblocksize*4;j++){
	float val=sin((j-f->qblocksize*3)*M_PI/(f->qblocksize*2));
	val=sin(val*val*M_PIl*.5);
	f->fftwf_buffer[0][j]*=sin(val*val*M_PIl*.5);
      }

      /* back to frequency; this is all-real data still */
      fftwf_execute(f->fftwf_forward[0]);
      for(j=0;j<f->qblocksize*4+2;j++)
	f->fftwf_buffer[0][j]/=f->qblocksize*4;
      
      /* now take what we learned and distill it a bit */
      f->ho_window[i]=calloc((f->qblocksize*2+1),sizeof(**f->ho_window));
      for(j=0;j<f->qblocksize*2+1;j++){
	f->ho_window[i][j]=f->fftwf_buffer[0][j*2];
	f->ho_area[i]+=fabs(f->ho_window[i][j]);
      }
      f->ho_area[i]=1./f->ho_area[i];

      lastf=thisf;
      
    }
  }

  return(0);
}

/* called only in playback thread */
int freq_reset(freq_state *f){
  /* reset cached pipe state */
  f->fillstate=0;
  while(pull_freq_feedback(f,NULL,NULL));
  return 0;
}

void freq_metric_work(float *work,freq_state *f,
		      float *sq_mags,float *peak,float *rms){
  int i,j;

  /* fill in metrics */
  memset(peak,0,sizeof(*peak)*f->bands);
  memset(rms,0,sizeof(*rms)*f->bands);

  for(i=0;i<f->qblocksize*2+1;i++)
    sq_mags[i]=(work[i*2]*work[i*2]+work[i*2+1]*work[i*2+1])*64.;
  
  for(i=0;i<f->bands;i++){
    float *ho_window=f->ho_window[i];
    for(j=0;j<f->qblocksize*2+1;j++){
      float val=fabs(sq_mags[j]*ho_window[j]);

      rms[i]+=val*.5;
      if(val>peak[i])peak[i]=val;
    }
    rms[i]=sqrt(rms[i]*f->ho_area[i]);
    peak[i]=sqrt(peak[i]);
  }

}

static void feedback_work(freq_state *f,float *peak,float *rms,
			  float *feedback_peak,float *feedback_rms){
  int i;
  for(i=0;i<f->bands;i++){
    feedback_rms[i]+=rms[i]*rms[i];
    if(feedback_peak[i]<peak[i])feedback_peak[i]=peak[i];
  }
}

static void lap_bypass(float *cache,float *in,float *lap,float *out,freq_state *f){
  int i,j;
  if(out){
    /* we're bypassing, so the input data hasn't spread from the half->block window.
       The lap may well be spread though, so we can't cheat and ignore the last third */
    for(i=0;i<f->qblocksize;i++)
      out[i]=lap[i];
  }

  /* keep lap up to date; bypassed data has not spread from the half-block padded window */
  for(i=0;i<f->qblocksize;i++)
    lap[i]=lap[i+f->qblocksize]+cache[i]*f->window[i];

  for(j=0;i<f->qblocksize*2;i++,j++)
    lap[i]=lap[i+f->qblocksize]+in[j]*f->window[i];

  memset(lap+f->qblocksize*2,0,f->qblocksize*sizeof(*lap));
}

static void lap_work(float *work,float *lap,float *out,freq_state *f){
  int i,j;
  
  /* lap and out */
  if(out)
    for(i=0;i<f->qblocksize;i++)
      out[i]=lap[i]+work[i];
  
  for(i=f->qblocksize,j=0;i<f->qblocksize*3;i++,j++)
    lap[j]=lap[i]+work[i];
  
  for(;i<f->qblocksize*4;i++,j++)
    lap[j]=work[i];
  
}

static void freq_transwork(freq_state *f,time_linkage *in){
  int i,j;
  for(i=0;i<input_ch;i++){
    /* extrapolation mechanism; avoid harsh transients at edges */
    if(f->fillstate==0)
      preextrapolate_helper(in->data[i],input_size,
			    f->cache[i],input_size);
    
    if(in->samples<in->size)
      postextrapolate_helper(f->cache[i],input_size,
			     in->data[i],in->samples,
			     in->data[i]+in->samples,
			     in->size-in->samples);
    
    memset(f->fftwf_buffer[i],0,
	   sizeof(**f->fftwf_buffer)*f->qblocksize);
    
    memcpy(f->fftwf_buffer[i]+f->qblocksize,
	   f->cache[i],
	   sizeof(**f->fftwf_buffer)*f->qblocksize);
    
    memcpy(f->fftwf_buffer[i]+f->qblocksize*2,
	   in->data[i],
	   sizeof(**f->fftwf_buffer)*f->qblocksize);
    
    memset(f->fftwf_buffer[i]+f->qblocksize*3,0,
	   sizeof(**f->fftwf_buffer)*f->qblocksize);
    
    for(j=0;j<f->qblocksize*2;j++)
      f->fftwf_buffer[i][j+f->qblocksize]*=.25*f->window[j]/f->qblocksize;
    /* transform the time data */
    
    fftwf_execute(f->fftwf_forward[i]);
  }
}


/* called only by playback thread */
time_linkage *freq_read(time_linkage *in, freq_state *f,
			void (*func)(freq_state *f,float **data,
				     float **peak, float **rms),
			int bypass){
  int i;

  float feedback_peak[input_ch][f->bands];
  float feedback_rms[input_ch][f->bands];

  float peak[input_ch][f->bands];
  float rms[input_ch][f->bands];

  float *peakp[input_ch];
  float *rmsp[input_ch];
  
  int blocks=0;

  if(!bypass){
    memset(feedback_peak,0,sizeof(feedback_peak));
    memset(feedback_rms,0,sizeof(feedback_rms));
  }

  for(i=0;i<input_ch;i++){
    peakp[i]=peak[i];
    rmsp[i]=rms[i];
  }
  
  switch(f->fillstate){
  case 0: /* prime the lapping and cache */
    if(in->samples==0){
      f->out.samples=0;
      return &f->out;
    }

    /* zero out lapping and cacahe state */
    for(i=0;i<input_ch;i++){
      memset(f->lap[i],0,sizeof(**f->lap)*f->qblocksize*3);
      memset(f->cache[i],0,sizeof(**f->cache)*input_size);
    }

    if(bypass){
      /* no need to do extra work; the panel is invisible (so no need
         to collect metrics) and the effect is inactive.  Just keep
         lapping state up to date and pass the data through.  Why
         bother with lapping?  We don't want a 'pop' when the effect
         is activated/deactivated */

      for(i=0;i<input_ch;i++)
	lap_bypass(f->cache[i],in->data[i],f->lap[i],0,f);

    }else{

      freq_transwork(f,in);
      func(f,f->fftwf_buffer,peakp,rmsp);
      blocks++;	
      
      for(i=0;i<input_ch;i++){
	feedback_work(f,peak[i],rms[i],feedback_peak[i],feedback_rms[i]);
	fftwf_execute(f->fftwf_backward[i]);
	lap_work(f->fftwf_buffer[i],f->lap[i],0,f);
	
      }
    }

    for(i=0;i<input_ch;i++){
      float *temp=in->data[i];
      memset(f->cache[i],0,sizeof(**f->cache)*input_size);
      in->data[i]=f->cache[i];
      f->cache[i]=temp;
    }
      
    f->cache_samples=in->samples;
    f->fillstate=1;
    f->out.samples=0;
    if(in->samples==in->size)goto tidy_up;
    
    for(i=0;i<input_ch;i++)
      memset(in->data[i],0,sizeof(**in->data)*in->size);
    in->samples=0;
    /* fall through */
    
  case 1: /* finish priming the lapping and cache */

    if(bypass){
      for(i=0;i<input_ch;i++)
	lap_bypass(f->cache[i],in->data[i],f->lap[i],0,f);
    }else{

      freq_transwork(f,in);
      func(f,f->fftwf_buffer,peakp,rmsp);
      blocks++;	
      
      for(i=0;i<input_ch;i++){
	feedback_work(f,peak[i],rms[i],feedback_peak[i],feedback_rms[i]);
	fftwf_execute(f->fftwf_backward[i]);
	lap_work(f->fftwf_buffer[i],f->lap[i],0,f);
	
      }
    }

    for(i=0;i<input_ch;i++){
      float *temp=in->data[i];
      memset(f->cache[i],0,sizeof(**f->cache)*input_size);
      in->data[i]=f->cache[i];
      f->cache[i]=temp;
    }
      
    f->cache_samples+=in->samples;
    f->fillstate=2;
    f->out.samples=0;
    if(in->samples==in->size)goto tidy_up;
    
    for(i=0;i<input_ch;i++)
      memset(in->data[i],0,sizeof(**in->data)*in->size);
    in->samples=0;
    /* fall through */
    
  case 2: /* nominal processing */

    if(bypass){
      for(i=0;i<input_ch;i++)
	lap_bypass(f->cache[i],in->data[i],f->lap[i],f->out.data[i],f);

    }else{

      freq_transwork(f,in);
      func(f,f->fftwf_buffer,peakp,rmsp);
      blocks++;	
      
      for(i=0;i<input_ch;i++){
	feedback_work(f,peak[i],rms[i],feedback_peak[i],feedback_rms[i]);
	fftwf_execute(f->fftwf_backward[i]);
	lap_work(f->fftwf_buffer[i],f->lap[i],f->out.data[i],f);
	
      }
    }

    for(i=0;i<input_ch;i++){
      float *temp=f->cache[i];
      f->cache[i]=in->data[i];
      in->data[i]=temp;
    }

    f->cache_samples+=in->samples;
    f->out.samples=(f->cache_samples>input_size?input_size:f->cache_samples);
    f->cache_samples-=f->out.samples;
    if(f->out.samples<f->out.size)f->fillstate=3;
    break;
  case 3: /* we've pushed out EOF already */
    f->out.samples=0;
  }
  
  /* finish up the state feedabck */
  if(!bypass && blocks){
    int j;
    float scale=1./blocks;
    freq_feedback *ff=
      (freq_feedback *)feedback_new(&f->feedpool,new_freq_feedback);

    if(!ff->peak[0])
      for(i=0;i<input_ch;i++)
	ff->peak[i]=malloc(f->bands*sizeof(**ff->peak));
    if(!ff->rms[0])
      for(i=0;i<input_ch;i++)
	ff->rms[i]=malloc(f->bands*sizeof(**ff->rms));

    for(i=0;i<input_ch;i++)
      for(j=0;j<f->bands;j++){
	feedback_rms[i][j]=todB(sqrt(feedback_rms[i][j]*scale));
	feedback_peak[i][j]=todB(feedback_peak[i][j]);
      }
      
    for(i=0;i<input_ch;i++){
      memcpy(ff->peak[i],feedback_peak[i],f->bands*sizeof(**feedback_peak));
      memcpy(ff->rms[i],feedback_rms[i],f->bands*sizeof(**feedback_rms));
    } 
    ff->bypass=0;
    feedback_push(&f->feedpool,(feedback_generic *)ff);
  }else{
    freq_feedback *ff=
      (freq_feedback *)feedback_new(&f->feedpool,new_freq_feedback);
    ff->bypass=1;
    feedback_push(&f->feedpool,(feedback_generic *)ff);
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
