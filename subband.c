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
#include "subband.h"
#include "lpc.h"

extern int input_size;
extern int input_rate;

/* called only by initial setup */
int subband_load(subband_state *f,int bands,int qblocksize,int ch){
  int i,j;
  memset(f,0,sizeof(*f));

  f->qblocksize=qblocksize;
  f->bands=bands;
  f->fillstate=0;
  f->lap_samples=0;
  f->lap=malloc(bands*sizeof(*f->lap));
  f->cache0=malloc(ch*sizeof(*f->cache0));
  f->cache1=malloc(ch*sizeof(*f->cache1));

  f->lap_activeP=malloc(ch*sizeof(*f->lap_activeP));
  f->lap_active1=malloc(ch*sizeof(*f->lap_active1));
  f->lap_active0=malloc(ch*sizeof(*f->lap_active0));
  f->lap_activeC=malloc(ch*sizeof(*f->lap_activeC));

  f->visible1=malloc(ch*sizeof(*f->visible1));
  f->visible0=malloc(ch*sizeof(*f->visible0));
  f->visibleC=malloc(ch*sizeof(*f->visibleC));
  
  f->effect_activeP=malloc(ch*sizeof(*f->effect_activeP));
  f->effect_active1=malloc(ch*sizeof(*f->effect_active1));
  f->effect_active0=malloc(ch*sizeof(*f->effect_active0));
  f->effect_activeC=malloc(ch*sizeof(*f->effect_activeC));

  f->wP=calloc(ch,sizeof(*f->wP));
  f->w1=calloc(ch,sizeof(*f->w1));
  f->w0=calloc(ch,sizeof(*f->w0));
  f->wC=calloc(ch,sizeof(*f->wC));

  for(i=0;i<ch;i++)
    f->cache0[i]=malloc(input_size*sizeof(**f->cache0));
  for(i=0;i<ch;i++)
    f->cache1[i]=malloc(input_size*sizeof(**f->cache1));

  for(i=0;i<bands;i++){
    f->lap[i]=malloc(ch*sizeof(**f->lap));
    for(j=0;j<ch;j++)
      f->lap[i][j]=malloc(input_size*3*sizeof(***f->lap));
  }

  f->out.channels=ch;
  f->out.data=malloc(ch*sizeof(*f->out.data));
  for(i=0;i<ch;i++)
    f->out.data[i]=malloc(input_size*sizeof(**f->out.data));

  /* fill in time window */
  f->window=malloc(f->qblocksize*2*sizeof(*f->window)); 
  /* we need a smooth-edged, symmetric window */
  for(i=0;i<f->qblocksize*2;i++)f->window[i]=sin(M_PIl*i/(f->qblocksize*2));
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

  /* unlike old postfish, we offer all frequencies via smoothly
     supersampling the spectrum */
  /* I'm too lazy to figure out the integral symbolically, use this
     fancy CPU thingy for something */
  
  w->ho_window=malloc(bands*sizeof(*w->ho_window));
  for(i=0;i<bands;i++)
    w->ho_window[i]=calloc((f->qblocksize*2+1),sizeof(**w->ho_window));

  /* first, build the first-pass desired, supersampled response */
  for(j=0;j<(((f->qblocksize*2+1)/10)<<5);j++){
    float localf= .5*j*input_rate/(f->qblocksize<<6);
    int localbin= j>>5;

    for(i=0;i<bands;i++){
      float lastf=(i>0?freq_list[i-1]:0);
      float thisf=freq_list[i];
      float nextf=freq_list[i+1];

      if(localf>=lastf && localf<nextf){
	float localwin=1.;
	if(localf<thisf){
	  if(i!=0)localwin= sin((localf-lastf)/(thisf-lastf)*M_PIl*.5);
	}else{
	  if(i+1!=bands)localwin= sin((nextf-localf)/(nextf-thisf)*M_PIl*.5);
	}

	localwin*=localwin;
	w->ho_window[i][localbin]+=localwin*(1./32);
      }
    }
  }
  j>>=5;
  for(;j<f->qblocksize*2+1;j++){
    float localf= .5*j*input_rate/(f->qblocksize<<1);

    for(i=0;i<bands;i++){
      float lastf=(i>0?freq_list[i-1]:0);
      float thisf=freq_list[i];
      float nextf=freq_list[i+1];

      if(localf>=lastf && localf<nextf){
	float localwin=1.;
	if(localf<thisf){
	  if(i!=0)localwin= sin((localf-lastf)/(thisf-lastf)*M_PIl*.5);
	}else{
	  if(i+1!=bands)localwin= sin((nextf-localf)/(nextf-thisf)*M_PIl*.5);
	}

	w->ho_window[i][j]+=localwin*localwin;
      }
    }
  }

  for(i=0;i<bands;i++){
    /* window each desired response in the time domain so that our
       convolution is properly padded against being circular */
    memset(f->fftwf_backward_in,0,sizeof(*f->fftwf_backward_in)*
	   (f->qblocksize*4+2));
    for(j=0;j<f->qblocksize*2+1;j++)
      f->fftwf_backward_in[j*2]=w->ho_window[i][j];
    
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
    for(j=0;j<f->qblocksize*2+1;j++)
      w->ho_window[i][j]=f->fftwf_forward_out[j*2];
    
  }
  
  return(0);
}

/* called only in playback thread */
int subband_reset(subband_state *f){
  /* reset cached pipe state */
  f->fillstate=0;
  return 0;
}

/* Brute force: subband the time data input by doing the equivalent of
   linear time-convolution using the response filters created earlier
   and padded FFTs with 75% overlap. */

static void subband_work(subband_state *f,
			 time_linkage *in,
			 subband_window **w,
			 int      *visible,
			 int      *active){


  int i,j,k,l,off,ch=f->out.channels;
  float *workoff=f->fftwf_forward_in+f->qblocksize;
  u_int32_t mutemask=in->active;

  f->mutemaskC=mutemask;

  for(i=0;i<ch;i++){

    int content_p=  f->lap_activeC[i]= (visible[i]||active[i]) && !mute_channel_muted(mutemask,i);
    int content_p0= f->lap_active0[i];
    int content_p1= f->lap_active1[i];

    int maxbands=w[i]->freq_bands;
    if(maxbands<f->w0[i]->freq_bands)maxbands=f->w0[i]->freq_bands;
    if(maxbands<f->w1[i]->freq_bands)maxbands=f->w1[i]->freq_bands;
    f->wC[i]=w[i];

    f->effect_activeC[i] = active[i] && !mute_channel_muted(mutemask,i);
    f->visibleC[i] = visible[i];

    if(!content_p1 && !content_p0 && !content_p){
      /* all disabled, lap is already zeroed.  Nothing to do */
      continue;

    }else if(!content_p0 && content_p){
      /* from inactive to active; first block to be processed must be
         entirely within new data so prepare the lap */

      for(k=0;k<maxbands;k++)
	memset(f->lap[k][i]+input_size*2,0,f->qblocksize*3*sizeof(***f->lap));

    }else if (content_p0 && !content_p){
      /* from active to inactive; the previous lap is done, just zero out this frame */
      for(k=0;k<maxbands;k++)
	memset(f->lap[k][i]+input_size*2,0,input_size*sizeof(*f->lap[k][i]));
    } 

    if(content_p){ /* there is something to do */

      if(content_p0){ /* was lap active last block? */
	j=0; /* yes; span the gap */
	off=0;
      }else{
	j=3; /* no; start entirely within new data */
	off=f->qblocksize;
      }

      for(;j<(input_size/f->qblocksize);j++){
	
	switch(j){
	case 0:
	  memcpy(workoff,f->cache0[i]+input_size-f->qblocksize*2,
		 sizeof(*workoff)*f->qblocksize*2);
	  break;
	case 1:	
	  memcpy(workoff,f->cache0[i]+input_size-f->qblocksize,
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
	for(k=0;k<w[i]->freq_bands;k++){
	  float *lapcb=f->lap[k][i]+input_size*2+(j-3)*f->qblocksize;
	  float *hw=w[i]->ho_window[k];

	  for(l=0;l<f->qblocksize*2+1;l++){
	    f->fftwf_backward_in[2*l]= 
	      f->fftwf_forward_out[2*l]*hw[l];
	    f->fftwf_backward_in[2*l+1]= 
	      f->fftwf_forward_out[2*l+1]*hw[l];
	  }
	  
	  fftwf_execute(f->fftwf_backward);
	  
	  /* lap back into the subband; the convolution is already
	     balanced so no further windowing needed */
	  for(l=0;l<f->qblocksize*3;l++)
	    lapcb[l]+=f->fftwf_backward_out[l];
	  for(;l<f->qblocksize*4;l++)
	    lapcb[l]=f->fftwf_backward_out[l];
	  
	}
      }
      /* if we're suddenly processing fewer bands than we were, we
	 have to trail out zeroes until the band lap is emptied */
      for(k=w[i]->freq_bands;k<maxbands;k++)
	memset(f->lap[k][i]+input_size*2,0,sizeof(*f->lap[k][i])*input_size);
	  
    }
  }
}

static void unsubband_work(subband_state *f,time_linkage *in, time_linkage *out){
  int i,j,k,ch=f->out.channels;

  f->lap_samples+=in->samples;
  for(i=0;i<ch;i++){
    
    int content_pP= f->lap_activeP[i];
    int content_p1= f->lap_active1[i];
    int content_p0= f->lap_active0[i];
    int content_pC= f->lap_activeC[i];

    int active_pP= f->effect_activeP[i];
    int active_p1= f->effect_active1[i];
    int active_p0= f->effect_active0[i];

    int muted_pP= mute_channel_muted(f->mutemaskP,i);
    int muted_p1= mute_channel_muted(f->mutemask1,i);
    int muted_p0= mute_channel_muted(f->mutemask0,i);

    int maxbands=f->wC[i]->freq_bands;
    if(maxbands<f->w0[i]->freq_bands)maxbands=f->w0[i]->freq_bands;
    if(maxbands<f->w1[i]->freq_bands)maxbands=f->w1[i]->freq_bands;
  
    /* even if the lapping for a channel is active, we will draw
       output from the cache if the effect is inactive; it saves
       performing the summation across bands */

    /*   OUTPUT PROCESSING

	 if muted, lap is inactive
	 if visible or active (and not muted) lap is active
	 if inactive and invisible, lap is inactive

            lap  effect mute 
	 1. (I)   (I)    A  : muted; do nothing, the mask will take care of it
	 2.  X     I     I  : full bypass, audible data; rotate the cache into output
	 3.  I     A     I  : not possible
	 4.  A     A     I  : nominal
	 
	 we transition from effect active/inactive/active:

	 a) transitions always happen in the shared active portion
	 b) sum frame from active lap
	 c) window beginning/end of resulting summation if necessary
	 d) window/add beginning/end of final frame from cache data if necessary

    */

    if(out){
      if(muted_p1 || !active_p1){
	/* case 1,2; they're similar enough  */
	
	float *temp=f->cache1[i];
	f->cache1[i]=out->data[i];
	out->data[i]=temp;
	/* mutemask will be propogated later */
	
      }else{
	/* 'other' */
	
	/* b) sum the lap */
	float *o=out->data[i];
	memcpy(o,f->lap[0][i],input_size*sizeof(*out->data[i]));
	for(j=1;j<maxbands;j++){
	  float *x=f->lap[j][i];
	  for(k=0;k<input_size;k++)
	    o[k]+=x[k];
	}
	
	/* c) window beginning/end of summation if needed */
	if(!active_pP && content_pP){

 	  /* transitioning to active effect, but the lap was already
	     previously active; window the transition */
	  float *lo=o+f->qblocksize;
	  float scale=f->qblocksize*4;
	  memset(o,0,sizeof(*o)*f->qblocksize);
	  for(j=0;j<f->qblocksize;j++)
	    lo[j]*=f->window[j]*scale;
	  
	}else if (!active_p0 && content_p0){

	  /* transitioning from active effect, but the lap will continue
	     to be active; window the transition */
	  float *lo=o+input_size-f->qblocksize*2;
	  float *lw=f->window+f->qblocksize;
	  float scale=f->qblocksize*4;
	  memset(o+input_size-f->qblocksize,0,sizeof(*o)*f->qblocksize);
	  for(j=0;j<f->qblocksize;j++)
	    lo[j]*=lw[j]*scale;

	} /* else any transitions we need are already windowed as
             above by the lap handling code in subband_work */
	
	/* d) window/add cache bits if needed */
	if(!active_pP && !muted_pP){
	  /* beginning transition */
	  float *lo=o+f->qblocksize;
	  float *lc=f->cache1[i]+f->qblocksize;
	  float *lw=f->window+f->qblocksize;
	  float scale=f->qblocksize*4;

	  memcpy(o,f->cache1[i],sizeof(*o)*f->qblocksize);
	  for(j=0;j<f->qblocksize;j++)
	    lo[j]+=lc[j]*lw[j]*scale;
	  
	}else if (!active_p0 && !muted_p0){
	  /* end transition */
	  float *lo=o+input_size-f->qblocksize*2;
	  float *lc=f->cache1[i]+input_size-f->qblocksize*2;
	  float scale=f->qblocksize*4;

	  memcpy(o+input_size-f->qblocksize,
		 f->cache1[i]+input_size-f->qblocksize,
		 sizeof(*o)*f->qblocksize);
	  for(j=0;j<f->qblocksize;j++)
	    lo[j]+=lc[j]*f->window[j]*scale;
	  
	}
      }

    }
    
    /* out is done for this channel */
    /* rotate lap */
    if(content_p1 || content_p0 || content_pC){
      for(j=0;j<maxbands;j++)
	memmove(f->lap[j][i],f->lap[j][i]+input_size,
		sizeof(***f->lap)*input_size*2);
      
      f->lap_activeP[i]=f->lap_active1[i];
      f->lap_active1[i]=f->lap_active0[i];
      f->lap_active0[i]=f->lap_activeC[i];
    }
    /* rotate cache data vectors */
    {
      float *temp=f->cache1[i];
      f->cache1[i]=f->cache0[i];
      f->cache0[i]=in->data[i];
      in->data[i]=temp;
    }
  }
  if(out){
    out->active=f->mutemask1;
    f->out.samples=(f->lap_samples>input_size?input_size:f->lap_samples);
  }

  /* rotate full-frame data for next frame */

  memcpy(f->effect_activeP,f->effect_active1,ch*sizeof(*f->effect_active1));
  memcpy(f->effect_active1,f->effect_active0,ch*sizeof(*f->effect_active0));
  memcpy(f->effect_active0,f->effect_activeC,ch*sizeof(*f->effect_activeC));

  memcpy(f->visible1,f->visible0,ch*sizeof(*f->visible0));
  memcpy(f->visible0,f->visibleC,ch*sizeof(*f->visibleC));

  f->mutemaskP=f->mutemask1;
  f->mutemask1=f->mutemask0;
  f->mutemask0=f->mutemaskC;

  memcpy(f->wP,f->w1,ch*sizeof(*f->w1));
  memcpy(f->w1,f->w0,ch*sizeof(*f->w0));
  memcpy(f->w0,f->wC,ch*sizeof(*f->wC));

  f->lap_samples-=(out?f->out.samples:0);

}

/* called only by playback thread */
time_linkage *subband_read(time_linkage *in, subband_state *f,
			   subband_window **w,int *visible, int *active,
			   void (*workfunc)(void *),void *arg){
  int i,j,ch=f->out.channels;
  
  switch(f->fillstate){
  case 0: /* begin priming the lapping and cache */
    if(in->samples==0){
      f->out.samples=0;
      return &f->out;
    }
    
    /* initially zero the lapping and cache */
    for(i=0;i<ch;i++){    
      for(j=0;j<f->bands;j++)    
	memset(f->lap[j][i],0,sizeof(***f->lap)*input_size*2);
      memset(f->cache1[i],0,sizeof(**f->cache1)*input_size);
      memset(f->cache0[i],0,sizeof(**f->cache0)*input_size);
    }
    f->lap_samples=0;

    /* and set up state variables */
    /* set the vars to 'active' so that if the first frame is an
       active frame, we don't transition window into it (the window
       would have been in the previous frame */
    for(i=0;i<ch;i++){
      int set=(visible[i]||active[i]) && !mute_channel_muted(in->active,i);
      f->lap_activeP[i]=set;
      f->lap_active1[i]=set;
      f->lap_active0[i]=set;

      f->wP[i]=w[i];
      f->w1[i]=w[i];
      f->w0[i]=w[i];

    }
    
    memcpy(f->effect_activeP,active,sizeof(*f->effect_activeP)*ch);
    memcpy(f->effect_active1,active,sizeof(*f->effect_active1)*ch);
    memcpy(f->effect_active0,active,sizeof(*f->effect_active0)*ch);
    //memset(f->effect_activeC,1,sizeof(*f->effect_activeC)*ch);

    memset(f->visible1,0,sizeof(*f->visible1)*ch);
    memset(f->visible0,0,sizeof(*f->visible0)*ch);
    
    f->mutemaskP=in->active;
    f->mutemask1=in->active;
    f->mutemask0=in->active;

    /* initially zero the padding of the input working array */
    memset(f->fftwf_forward_in,0,f->qblocksize*4*sizeof(*f->fftwf_forward_in));

    /* extrapolation mechanism; avoid harsh transients at edges */
    for(i=0;i<ch;i++){
      
      if(f->lap_active0[i]){
	preextrapolate_helper(in->data[i],input_size,
			    f->cache0[i],input_size);
      
	if(in->samples<input_size)
	  postextrapolate_helper(f->cache0[i],input_size,
				 in->data[i],in->samples,
				 in->data[i]+in->samples,
				 input_size-in->samples);
      }
    }
      
    subband_work(f,in,w,visible,active);
    workfunc(arg);
    unsubband_work(f,in,NULL);

    f->fillstate=1;
    f->out.samples=0;
    if(in->samples==input_size)goto tidy_up;
    
    for(i=0;i<ch;i++)
      memset(in->data[i],0,sizeof(**in->data)*input_size);
    in->samples=0;
    /* fall through */

  case 1: /* finish priming the lapping and cache */

    /* extrapolation mechanism; avoid harsh transients at edges */

    if(in->samples<input_size)
      for(i=0;i<ch;i++){
	if((active[i] || visible[i]) && !mute_channel_muted(in->active,i))
	  postextrapolate_helper(f->cache0[i],input_size,
				 in->data[i],in->samples,
				 in->data[i]+in->samples,
				 input_size-in->samples);
      }
      
    subband_work(f,in,w,visible,active);
    workfunc(arg);
    unsubband_work(f,in,NULL);

    f->fillstate=2;
    f->out.samples=0;
    if(in->samples==input_size)goto tidy_up;
    
    for(i=0;i<ch;i++)
      memset(in->data[i],0,sizeof(**in->data)*input_size);
    in->samples=0;
    /* fall through */

  case 2: /* nominal processing */
    
    if(in->samples<input_size)
      for(i=0;i<ch;i++){
	if((active[i] || visible[i]) && !mute_channel_muted(in->active,i))
	  postextrapolate_helper(f->cache0[i],input_size,
				 in->data[i],in->samples,
				 in->data[i]+in->samples,
				 input_size-in->samples);
      }
      
    subband_work(f,in,w,visible,active);
    workfunc(arg);
    unsubband_work(f,in,&f->out);

    if(f->out.samples<input_size)f->fillstate=3;
    break;

  case 3: /* we've pushed out EOF already */
    f->out.samples=0;
    break;
  }

 tidy_up:
  {
    int tozero=input_size-f->out.samples;
    if(tozero)
      for(i=0;i<f->out.channels;i++)
        memset(f->out.data[i]+f->out.samples,0,sizeof(**f->out.data)*tozero);
  }
  
  return &f->out;
}
