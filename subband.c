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
#include <fftw3.h>
#include "feedback.h"
#include "subband.h"
#include "lpc.h"

extern int input_size;
extern int input_rate;
extern int input_ch;

/* feedback! */
typedef struct subband_feedback{
  feedback_generic parent_class;
  float **peak;
  float **rms;
  int freq_bands;
  int bypass;
} subband_feedback;

static feedback_generic *new_subband_feedback(void){
  subband_feedback *ret=calloc(1,sizeof(*ret));
  return (feedback_generic *)ret;
}

/* total, peak, rms are pulled in array[freqs][input_ch] order */

int pull_subband_feedback(subband_state *ff,float **peak,float **rms,int *b){
  subband_feedback *f=(subband_feedback *)feedback_pull(&ff->feedpool);
  int i;
  
  if(!f)return 0;
  
  if(f->bypass){
    feedback_old(&ff->feedpool,(feedback_generic *)f);
    return 2;
  }else{
    if(peak)
      for(i=0;i<ff->bands;i++)
	memcpy(peak[i],f->peak[i],sizeof(**peak)*input_ch);
    if(rms)
      for(i=0;i<ff->bands;i++)
	memcpy(rms[i],f->rms[i],sizeof(**rms)*input_ch);
    if(b)*b=f->freq_bands;
    feedback_old(&ff->feedpool,(feedback_generic *)f);
    return 1;
  }
}

/* called only by initial setup */
int subband_load(subband_state *f,int bands,int qblocksize){
  int i,j;
  memset(f,0,sizeof(*f));

  f->qblocksize=qblocksize;
  f->bands=bands;
  f->fillstate=0;
  f->lap_samples=0;
  f->lap=malloc(bands*sizeof(*f->lap));
  f->cache=malloc(input_ch*sizeof(*f->cache));
  
  for(i=0;i<input_ch;i++)
    f->cache[i]=malloc(input_size*sizeof(**f->cache));

  for(i=0;i<bands;i++){
    f->lap[i]=malloc(input_ch*sizeof(**f->lap));
    for(j=0;j<input_ch;j++)
      f->lap[i][j]=malloc(input_size*3*sizeof(***f->lap));
  }

  f->out.size=input_size;
  f->out.channels=input_ch;
  f->out.rate=input_rate;
  f->out.data=malloc(input_ch*sizeof(*f->out.data));
  for(i=0;i<input_ch;i++)
    f->out.data[i]=malloc(input_size*sizeof(**f->out.data));

  /* fill in time window */
  f->window=malloc(f->qblocksize*2*sizeof(*f->window)); 
  /* we need a smooth-edged, symmetric window */
  for(i=0;i<f->qblocksize*2;i++)f->window[i]=sin(M_PIl*i/(f->qblocksize*2));
  //for(i=0;i<f->qblocksize*2;i++)f->window[i]*=f->window[i];
  //for(i=0;i<f->qblocksize*2;i++)f->window[i]=sin(f->window[i]*M_PIl*.5);
  for(i=0;i<f->qblocksize*2;i++)f->window[i]*=f->window[i]*.25/f->qblocksize;

  f->fftwf_forward_out  = fftwf_malloc(sizeof(*f->fftwf_forward_out) * 
				       (f->qblocksize*4+2));
  f->fftwf_backward_in  = fftwf_malloc(sizeof(*f->fftwf_backward_in) * 
				       (f->qblocksize*4+2));
  f->fftwf_forward_in   = fftwf_malloc(sizeof(*f->fftwf_forward_in) * 
				       f->qblocksize*4);
  f->fftwf_backward_out = fftwf_malloc(sizeof(*f->fftwf_backward_out) * 
				       f->qblocksize*4);
  
  f->fftwf_forward=fftwf_plan_dft_r2c_1d(f->qblocksize*4, 
					 f->fftwf_forward_in, 
					 (fftwf_complex *)f->fftwf_forward_out, 
					 FFTW_MEASURE);
  f->fftwf_backward=fftwf_plan_dft_c2r_1d(f->qblocksize*4,
					  (fftwf_complex *)f->fftwf_backward_in, 
					  f->fftwf_backward_out, 
					  FFTW_MEASURE);

  return(0);
}

/* called only by initial setup */
int subband_load_freqs(subband_state *f,subband_window *w,
		       const float *freq_list,int bands){
  int i,j;

  memset(w,0,sizeof(*w));

  w->freq_bands=bands;

  /* supersample the spectrum */
  w->ho_window=malloc(bands*sizeof(*w->ho_window));
  w->ho_area=calloc(bands,sizeof(*w->ho_area));
  {
    float working[f->qblocksize*4+2];
    
    for(i=0;i<bands;i++){
      float lastf=(i>0?freq_list[i-1]:0);
      float thisf=freq_list[i];
      float nextf=freq_list[i+1];
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
      memset(f->fftwf_backward_in,0,sizeof(*f->fftwf_backward_in)*
	     (f->qblocksize*4+2));
      for(j=0;j<f->qblocksize*2;j++)
	f->fftwf_backward_in[j*2]=working[j];
      
      fftwf_execute(f->fftwf_backward);
      
      /* window response in time */
      memcpy(f->fftwf_forward_in,f->fftwf_backward_out,
	     f->qblocksize*4*sizeof(*f->fftwf_forward_in));
      for(j=0;j<f->qblocksize;j++){
	float val=cos(j*M_PI/(f->qblocksize*2));
	val=sin(val*val*M_PIl*.5);
	f->fftwf_forward_in[j]*= sin(val*val*M_PIl*.5);
      }
      
      for(;j<f->qblocksize*3;j++)
	f->fftwf_forward_in[j]=0.;
      
      for(;j<f->qblocksize*4;j++){
	float val=sin((j-f->qblocksize*3)*M_PI/(f->qblocksize*2));
	val=sin(val*val*M_PIl*.5);
	f->fftwf_forward_in[j]*=sin(val*val*M_PIl*.5);
      }
      
      /* back to frequency; this is all-real data still */
      fftwf_execute(f->fftwf_forward);
      for(j=0;j<f->qblocksize*4+2;j++)
	f->fftwf_forward_out[j]/=f->qblocksize*4;
      
      /* now take what we learned and distill it a bit */
      w->ho_window[i]=calloc((f->qblocksize*2+1),sizeof(**w->ho_window));
      for(j=0;j<f->qblocksize*2+1;j++)
	w->ho_area[i]+=w->ho_window[i][j]=f->fftwf_forward_out[j*2];
      
      lastf=thisf;
      
    }
  }
  
  return(0);
}

/* called only in playback thread */
int subband_reset(subband_state *f){
  /* reset cached pipe state */
  f->fillstate=0;
  while(pull_subband_feedback(f,NULL,NULL,NULL));
  return 0;
}

/* Brute force: subband the time data input by doing the equivalent of
   linear time-convolution using the response filters created earlier
   and padded FFTs with 75% overlap. */

static void subband_work(subband_state *f,time_linkage *in,subband_window *w){
  int i,j,k,l,off;
  float *workoff=f->fftwf_forward_in+f->qblocksize;

  for(i=0;i<input_ch;i++){
    off=0;

    for(j=0;j<(input_size/f->qblocksize);j++){
      
      switch(j){
      case 0:
	memcpy(workoff,f->cache[i]+input_size-f->qblocksize*2,
	       sizeof(*workoff)*f->qblocksize*2);
	break;
      case 1:	
	memcpy(workoff,f->cache[i]+input_size-f->qblocksize,
	       sizeof(*workoff)*f->qblocksize);
	memcpy(workoff+f->qblocksize,in->data[i],
	       sizeof(*workoff)*f->qblocksize);
	break;
      default:
	memcpy(workoff,in->data[i]+off,
	       sizeof(*workoff)*f->qblocksize*2);
	off+=f->qblocksize;
	break;
      }

      /* window; assume the edges are already zeroed */
      for(k=0;k<f->qblocksize*2;k++)workoff[k]*=f->window[k];

      fftwf_execute(f->fftwf_forward);

      /* repeatedly filter and transform back */
      for(k=0;k<w->freq_bands;k++){
	float *lapcb=f->lap[k][i]+input_size*2+(j-3)*f->qblocksize;
	
	for(l=0;l<f->qblocksize*2+1;l++){
	  f->fftwf_backward_in[2*l]= 
	    f->fftwf_forward_out[2*l]*w->ho_window[k][l];
	  f->fftwf_backward_in[2*l+1]= 
	    f->fftwf_forward_out[2*l+1]*w->ho_window[k][l];
	}
	
	fftwf_execute(f->fftwf_backward);
	
	/* lap back into the subband; the convolution is already
           balanced so no further windowing needed */
	for(l=0;l<f->qblocksize*3;l++)
	  lapcb[l]+=f->fftwf_backward_out[l];
	for(;l<f->qblocksize*4;l++)
	  lapcb[l]=f->fftwf_backward_out[l];

      }
      /* if we're suddenly processing fewer bands than we were, we
         have to trail out zeroes until the band lap is emptied */
      {
	int bands=f->lapbands[0];
	if(bands<f->lapbands[1])bands=f->lapbands[1];
	if(bands<f->lapbands[2])bands=f->lapbands[2];

	for(;k<bands;k++){
	  float *lapcb=f->lap[k][i]+input_size*2+(j-3)*f->qblocksize;
	  memset(lapcb+f->qblocksize*3,0,sizeof(*lapcb)*f->qblocksize);
	}
      }
    }
  }
}

static void bypass_work(subband_state *f,time_linkage *in){
  int i,j;
  float scale=f->qblocksize*4.;

  for(i=0;i<input_ch;i++){
    float *lapcb=f->lap[0][i]+input_size*2-f->qblocksize*2;
    float *x=f->cache[i]+input_size-f->qblocksize*2;
	
    for(j=0;j<f->qblocksize;j++)
      lapcb[j]+=x[j]*f->window[j]*scale;

    lapcb+=f->qblocksize;
    x+=f->qblocksize;
    
    memcpy(lapcb,x,sizeof(*x)*f->qblocksize);

    lapcb+=f->qblocksize;
    x=in->data[i];

    memcpy(lapcb,x,sizeof(*x)*(input_size-f->qblocksize*2));

    lapcb+=(input_size-f->qblocksize*2);
    x+=(input_size-f->qblocksize*2);

    for(j=0;j<f->qblocksize;j++)
      lapcb[j]=x[j]*f->window[j+f->qblocksize]*scale;

    for(;j<f->qblocksize*2;j++)
      lapcb[j]=0.;

    /* do we need to keep propogating zeros forward in other bands? */
    {
      int bands=f->lapbands[0];
      if(bands<f->lapbands[1])bands=f->lapbands[1];
      if(bands<f->lapbands[2])bands=f->lapbands[2];
      
      for(j=1;j<bands;j++){
	float *lapcb=f->lap[j][i]+input_size*2-f->qblocksize;
	memset(lapcb,0,sizeof(*lapcb)*(input_size+f->qblocksize));
      }
    }
  }
}

static void unsubband_work(subband_state *f,float **out,int inbands){
  int i,j,k;

  int bands=f->lapbands[0];
  if(bands<f->lapbands[1])bands=f->lapbands[1];
  if(bands<f->lapbands[2])bands=f->lapbands[2];
  if(bands<inbands)bands=inbands;

  for(i=0;i<input_ch;i++){
    for(j=bands-1;j>=0;j--){
      
      /* add bands back together for output */
      if(out){
	if(j==bands-1){
	  memcpy(out[i],f->lap[j][i],input_size*(sizeof **out));
	}else{
	  for(k=0;k<input_size;k++)
	    out[i][k]+=f->lap[j][i][k];
	}
      }
      
      /* shift bands for next lap */
      /* optimization target: ringbuffer me! */
      memmove(f->lap[j][i],f->lap[j][i]+input_size,
	      sizeof(***f->lap)*input_size*2);
      
    }
  }

  f->lapbands[0]=f->lapbands[1];
  f->lapbands[1]=f->lapbands[2];
  f->lapbands[2]=inbands;

}

/* called only by playback thread */
time_linkage *subband_read(time_linkage *in, subband_state *f,
			   subband_window *w,
			   void (*func)(float **, float **),int bypass){
  int i,j;
  float peak[w->freq_bands][input_ch];
  float rms[w->freq_bands][input_ch];
  
  float *peakp[w->freq_bands];
  float *rmsp[w->freq_bands];
  
  if(!bypass)
    for(i=0;i<w->freq_bands;i++){
      peakp[i]=peak[i];
      rmsp[i]=rms[i];
    }
  
  switch(f->fillstate){
  case 0: /* begin priming the lapping and cache */
    if(in->samples==0){
      f->out.samples=0;
      return &f->out;
    }
    
    /* initially zero the lapping */
    for(i=0;i<input_ch;i++){    
      for(j=0;j<f->bands;j++)    
	memset(f->lap[j][i],0,sizeof(***f->lap)*input_size*2);
      memset(f->cache[i],0,sizeof(**f->cache)*input_size);
    }

    /* initially zero the padding of the input working array */
    memset(f->fftwf_forward_in,0,f->qblocksize*4*sizeof(*f->fftwf_forward_in));

    if(bypass){
      bypass_work(f,in);
      unsubband_work(f,0,1);
    }else{
      /* extrapolation mechanism; avoid harsh transients at edges */
      for(i=0;i<input_ch;i++){
	preextrapolate_helper(in->data[i],input_size,
			      f->cache[i],input_size);
      
      if(in->samples<in->size)
	postextrapolate_helper(f->cache[i],input_size,
			       in->data[i],in->samples,
			       in->data[i]+in->samples,
			       in->size-in->samples);
      }
      
      subband_work(f,in,w);
      func(peakp,rmsp);
      unsubband_work(f,0,w->freq_bands);
    }

    for(i=0;i<input_ch;i++){
      float *temp=in->data[i];
      in->data[i]=f->cache[i];
      f->cache[i]=temp;
    }
    
    f->lap_samples=in->samples;
    f->fillstate=1;
    f->out.samples=0;
    if(in->samples==in->size)goto tidy_up;
    
    for(i=0;i<input_ch;i++)
      memset(in->data[i],0,sizeof(**in->data)*in->size);
    in->samples=0;
    /* fall through */

  case 1: /* finish priming the lapping and cache */

    if(bypass){
      bypass_work(f,in);
      unsubband_work(f,0,1);
    }else{
      /* extrapolation mechanism; avoid harsh transients at edges */
      for(i=0;i<input_ch;i++){
	if(in->samples<in->size)
	  postextrapolate_helper(f->cache[i],input_size,
				 in->data[i],in->samples,
				 in->data[i]+in->samples,
				 in->size-in->samples);
      }
      
      subband_work(f,in,w);
      func(peakp,rmsp);
      unsubband_work(f,0,w->freq_bands);
    }

    for(i=0;i<input_ch;i++){
      float *temp=in->data[i];
      in->data[i]=f->cache[i];
      f->cache[i]=temp;
    }
    
    f->lap_samples=in->samples;
    f->fillstate=2;
    f->out.samples=0;
    if(in->samples==in->size)goto tidy_up;
    
    for(i=0;i<input_ch;i++)
      memset(in->data[i],0,sizeof(**in->data)*in->size);
    in->samples=0;
    /* fall through */

  case 2: /* nominal processing */
    
    if(bypass){
      bypass_work(f,in);
      unsubband_work(f,f->out.data,1);
    }else{
      /* extrapolation mechanism; avoid harsh transients at edges */
      for(i=0;i<input_ch;i++){
	if(in->samples<in->size)
	  postextrapolate_helper(f->cache[i],input_size,
				 in->data[i],in->samples,
				 in->data[i]+in->samples,
				 in->size-in->samples);
      }
      
      subband_work(f,in,w);
      func(peakp,rmsp);
      unsubband_work(f,f->out.data,w->freq_bands);
    }
    for(i=0;i<input_ch;i++){
      float *temp=in->data[i];
      in->data[i]=f->cache[i];
      f->cache[i]=temp;
    }
    
    f->lap_samples+=in->samples;
    f->fillstate=2;
    f->out.samples=(f->lap_samples>input_size?input_size:f->lap_samples);
    f->lap_samples-=f->out.samples;
    if(f->out.samples<f->out.size)f->fillstate=2;
    break;

  case 3: /* we've pushed out EOF already */
    f->out.samples=0;
    break;
  }

  /* finish up the state feedabck */
  if(!bypass){
    subband_feedback *ff=
      (subband_feedback *)feedback_new(&f->feedpool,new_subband_feedback);

    if(!ff->peak){
      ff->peak=malloc(f->bands*sizeof(*ff->peak));
      for(i=0;i<f->bands;i++)
	ff->peak[i]=malloc(input_ch*sizeof(**ff->peak));
    }
    if(!ff->rms){
      ff->rms=malloc(f->bands*sizeof(*ff->rms));
      for(i=0;i<f->bands;i++)
	ff->rms[i]=malloc(input_ch*sizeof(**ff->rms));
    }

    for(i=0;i<w->freq_bands;i++){
      memcpy(ff->peak[i],peak[i],input_ch*sizeof(**peak));
      memcpy(ff->rms[i],rms[i],input_ch*sizeof(**rms));
    } 
    ff->bypass=0;
    ff->freq_bands=w->freq_bands;
    feedback_push(&f->feedpool,(feedback_generic *)ff);
  }else{
    subband_feedback *ff=
      (subband_feedback *)feedback_new(&f->feedpool,new_subband_feedback);
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
