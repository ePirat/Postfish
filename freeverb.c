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

/* Derived from the Freeverb3 public domain reverb code by Jezar at
   Dreampoint. This C version of the original C++ assumes reverb to be
   a solved problem and so is not structured for tinkering; it removes
   much of the pretty OO to make it a monolithic black box. */

#include "postfish.h"
#include "internal.h"
#include "freeverb.h"
#include "window.h"

static void inject_set(reverb_state *r,int inject){
  int i;
  for(i=0;i<numcombs;i++){
    int off=(1000-inject)*r->comb[i].size/scaleroom;
    r->comb[i].extpending=r->comb[i].injptr-off;
    if(r->comb[i].extpending<r->comb[i].buffer)r->comb[i].extpending+=r->comb[i].size;
  }
}

static void inject_finalize(reverb_state *r){
  int i;
  for(i=0;i<numcombs;i++)
    r->comb[i].extptr=r->comb[i].extpending;
}
 
static void reset_one(reverb_state *r,int inject, const int *combtuning, const int *alltuning){
  int i;
  memset(r,0,sizeof(*r));
  
  r->comb[0].buffer=r->bufcomb0;
  r->comb[1].buffer=r->bufcomb1;
  r->comb[2].buffer=r->bufcomb2;
  r->comb[3].buffer=r->bufcomb3;
  r->comb[4].buffer=r->bufcomb4;
  r->comb[5].buffer=r->bufcomb5;
  r->comb[6].buffer=r->bufcomb6;
  r->comb[7].buffer=r->bufcomb7;

  for(i=0;i<numcombs;i++)
    r->comb[i].size=combtuning[i];
  for(i=0;i<numcombs;i++)
    r->comb[i].injptr=r->comb[i].buffer;

  r->allpass[0].buffer=r->bufallpass0;
  r->allpass[1].buffer=r->bufallpass1;
  r->allpass[2].buffer=r->bufallpass2;
  r->allpass[3].buffer=r->bufallpass3;
  for(i=0;i<numallpasses;i++)
    r->allpass[i].size=alltuning[i];
  for(i=0;i<numallpasses;i++)
    r->allpass[i].bufptr=r->allpass[i].buffer;

  inject_set(r,inject);
  for(i=0;i<numcombs;i++)
    r->comb[i].extptr=r->comb[i].extpending;
}

static inline float allpass_process(allpass_state *a,
				    float  input){
  float val    = *a->bufptr;
  float output = val - input;
  
  *a->bufptr   = val * .5f + input;
  underguard(a->bufptr);
  
  if(a->bufptr<=a->buffer) a->bufptr += a->size;
  --a->bufptr;

  return output;
}

static inline float comb_process(comb_state *c,
				 float  feedback,
				 float  hfdamp,
				 float  input){
  float val      = *c->extptr;
  c->filterstore = val + (c->filterstore - val)*hfdamp;
  underguard(&c->filterstore);

  *c->injptr     = input + c->filterstore * feedback;
  underguard(c->injptr);


  if(c->injptr<=c->buffer) c->injptr += c->size;
  --c->injptr;
  if(c->extptr<=c->buffer) c->extptr += c->size;
  --c->extptr;

  return val;
}

static inline float comb_process_exttrans(comb_state *c,
					  float  transval,
					  float  feedback,
					  float  hfdamp,
					  float  input){
  float val      = *c->extpending*(1.f-transval) + *c->extptr*transval;
  c->filterstore = val + (c->filterstore - val)*hfdamp;
  underguard(&c->filterstore);

  *c->injptr     = input + c->filterstore * feedback;
  underguard(c->injptr);


  if(c->injptr<=c->buffer) c->injptr += c->size;
  --c->injptr;
  if(c->extptr<=c->buffer) c->extptr += c->size;
  --c->extptr;
  if(c->extpending<=c->buffer) c->extpending += c->size;
  --c->extpending;

  return val;
}

static void reverb_instance_one_reset(reverb_instance_one *rio){
  rio->initstate=0;
}

void reverb_instance_reset(reverb_instance *ri){
  int i;
  ri->initstate=0;
  for(i=0;i<ri->ch;i++)
    reverb_instance_one_reset(ri->reverbs+i);
}

static void process_one_inner(reverb_state *r, 
			      float feedback, float hfdamp, float att,
			      float *input, float *output, long n){
  float out,val=0;
  int i;

  att=fromdB(att) * fixedgain;

  while(n-- > 0){
    out = 0;
    if(input) val = *input++;

    for(i=0;i<numcombs;i++)
      out += comb_process(r->comb+i,feedback,hfdamp,val);
    
    for(i=0;i<numallpasses;i++)
      out  = allpass_process(r->allpass+i,out);
    
    if(output) *output++ = out*att;
  }
}

static void process_one_inner2(reverb_state *r, float *transwindow,
			       float feedback1, float feedback2, 
			       float hfdamp1, float hfdamp2, 
			       float att1, float att2,
			       int inject1, int inject2,
			       float *input1, long n1,
			       float *input2, long n2,
			       float *output){

  int n=n1+n2,i;
  float out,val=0;
  float *input=input1;

  if(inject1 != inject2) inject_set(r,inject2);

  while(n > 0){
    float feedback= transwindow[n]*(feedback1-feedback2) + feedback2;
    float hfdamp= transwindow[n]*(hfdamp1-hfdamp2) + hfdamp2;
    float att = fromdB(transwindow[n]*(att1-att2) + att2) * fixedgain;

    if(n == n2)input=input2;
    n--;

    out = 0;
    if(input) val = *input++;

    if(inject1 != inject2){
      for(i=0;i<numcombs;i++)
	out += comb_process_exttrans(r->comb+i,transwindow[n],feedback,hfdamp,val);
    }else{
      for(i=0;i<numcombs;i++)
	out += comb_process(r->comb+i,feedback,hfdamp,val);
    }

    for(i=0;i<numallpasses;i++)
      out  = allpass_process(r->allpass+i,out);

    if(output) *output++ = out*att;

  }


  if(inject1 != inject2)inject_finalize(r);

}

static void process_one_wrapper(reverb_state *r,
				float *transwindow,
				float feedback, float feedback2, 
				float hfdamp, float hfdamp2, 
				float att, float att2,
				int inject, int inject2, 
				float *input1, long n1,
				float *input2, long n2,
				float *output){
  if(att != att2 ||
     hfdamp != hfdamp2 ||
     feedback != feedback2 ||
     inject != inject2 ){
    
    process_one_inner2(r,transwindow,
		       feedback,feedback2,hfdamp,hfdamp2,att,att2,
		       inject,inject2,input1,n1,input2,n2,output);
  }else{
    process_one_inner(r,feedback,hfdamp,att,input1,output,n1);      
    process_one_inner(r,feedback,hfdamp,att,input2,output+n1,n2);
  }
}

/* returns active or inactive status */
static u_int32_t process_one(reverb_instance_one *ri, reverb_settings *s, 
			     float *in, int muted, float *outL, float *outR, 
			     float *transwindow, long n, long blocksize){
  
  float *cache=ri->cache;
  int i;
  int energy=0;

  /* convert settings */
  float feedback = (s->liveness*scaleliveness/1000.f+offsetliveness);
  float hfdamp   = s->hfdamp*.001*scalehfdamp;
  float wet      = s->wet * .1f;
  int   width    = s->width;
  float wet1     = sin(width * M_PI * .0005)*.5f+.5f;
  float wet2     = cos(width * M_PI * .0005)*.5;
  int   delay    = s->delay *blocksize/1000; 
  int   inject   = s->roomsize;

  int   first    = delay;
  int   second   = n-first;
  int   active   = s->active;

  if(!ri->initstate){
    ri->sC.feedback=feedback;
    ri->sC.hfdamp=hfdamp;
    ri->sC.wet=wet;
    ri->sC.wet1=wet1;
    ri->sC.wet2=wet2;
    ri->sC.width=width;
    ri->sC.delay=delay;
    ri->sC.inject=inject;
    ri->sC.active=0; /* soft start */
    ri->initstate=1;
    
    if(ri->rL)reset_one(ri->rL,inject,combL,allL);
    if(ri->rR)reset_one(ri->rR,inject,combR,allR);

    /* clear cache to empty */
    memset(cache,0,blocksize*sizeof(*cache));
  }

  /* if this frame is inactive or muted, let the reverb 'ring' out.
     if the reverb tail has rung out, nothing to do */
  if((!ri->sC.active && !active) || muted){

    if(ri->rL && ri->rL->energy){

      process_one_wrapper(ri->rL,transwindow,
			  ri->sC.feedback,feedback,
			  ri->sC.hfdamp,hfdamp,
			  ri->sC.wet,wet,
			  ri->sC.inject,inject,
			  0,n,0,0,outL);      
      
      for(i=0;i<n;i++)if(outL[i]*outL[i] > 1e-15f)break;
      if(i==n)
	ri->rL->energy=0;
      else
	energy=1;
      
    }

    if(ri->rR && ri->rR->energy){
      process_one_wrapper(ri->rR,transwindow,
			  ri->sC.feedback,feedback,
			  ri->sC.hfdamp,hfdamp,
			  ri->sC.wet,wet,
			  ri->sC.inject,inject,
			  0,n,0,0,outR);  

      for(i=0;i<n;i++)if(outR[i]*outR[i] > 1e-15f)break;
      if(i==n)
	ri->rR->energy=0;
      else
	energy=1;
    }

  }else{

    /* we have input and are active or transitioning */

    /* if the previous delay and delay this frame differ, build a
       smoothed transition using the cache buffer as working space */
    if(delay != ri->sC.delay){
      float *ptrC=cache + n - ri->sC.delay;
      float *ptr =cache + n - delay;
      
      int firstC=ri->sC.delay;
      
      for(i=0;i<n;i++){
	if(i==firstC)ptrC=in;
	if(i==first)ptr=in;
	cache[i]=transwindow[i]*(*ptr++) + 
	  (1.f-transwindow[i])*(*ptrC++);
      }
      
      first=n;
      second=0;
    }

    /* If we're active now but previous frame was inactive, build a
       soft-started input into the cache.  Not needed for muted
       frames, as muted transitions are soft-started already. */
    if(active && !ri->sC.active){
      float *ptr = cache + n - first;

      for(i=0;i<n;i++){
	if(i==first)ptr=in;
	cache[i]=transwindow[i]* *ptr++;
      }

      first=n;
      second=0;
    }

    /* As above, but active -> inactive */
    if(!active && ri->sC.active){
      float *ptr = cache + n - first;

      for(i=0;i<n;i++){
	if(i==first)ptr=in;
	cache[i]=transwindow[n-i]* *ptr++;
      }

      first=n;
      second=0;
    }

    /* run the filters */
    if(ri->rL){
      ri->rL->energy=1;
      energy=1;
      process_one_wrapper(ri->rL,transwindow,
			  ri->sC.feedback,feedback,
			  ri->sC.hfdamp,hfdamp,
			  ri->sC.wet,wet,
			  ri->sC.inject,inject,
			  cache,first,in,second,outL);      
    }

    if(ri->rR){
      ri->rR->energy=1;
      energy=1;
      process_one_wrapper(ri->rR,transwindow,
			  ri->sC.feedback,feedback,
			  ri->sC.hfdamp,hfdamp,
			  ri->sC.wet,wet,
			  ri->sC.inject,inject,
			  cache,first,in,second,outR);
    }
  }

  /* all or none consistency check */
  if(ri->rL && ri->rR && energy){
    if(!ri->rL->energy)
      memset(outL,0,sizeof(*outL)*blocksize);
    if(!ri->rR->energy)
      memset(outR,0,sizeof(*outR)*blocksize);
  }

  /* stereo butterfly? */
  if(ri->rL && ri->rR && energy &&
     (width<1000 || ri->sC.width<1000)){

    /* static butterfly or transitional? */
    if(width != ri->sC.width){
      float wetdel1=(wet1-ri->sC.wet1)/n;
      float wetacc1=ri->sC.wet1;
      float wetdel2=(wet2-ri->sC.wet2)/n;
      float wetacc2=ri->sC.wet2;

      for(i=0;i<n;i++){
	float newL=outL[i]*wetacc1 + outR[i]*wetacc2;
	float newR=outR[i]*wetacc1 + outL[i]*wetacc2;

	outL[i]=newL;
	outR[i]=newR;
	wetacc1+=wetdel1;
	wetacc2+=wetdel2;
	
      }
      
    }else{
      for(i=0;i<n;i++){
	float newL=outL[i]*wet1 + outR[i]*wet2;
	float newR=outR[i]*wet1 + outL[i]*wet2;

	outL[i]=newL;
	outR[i]=newR;
      }
    }
  }

  ri->sC.feedback=feedback;
  ri->sC.hfdamp=hfdamp;
  ri->sC.wet=wet;
  ri->sC.wet1=wet1;
  ri->sC.wet2=wet2;
  ri->sC.width=width;
  ri->sC.delay=delay;
  ri->sC.inject=inject;
  ri->sC.active=active;

  return energy;
}

void reverb_settings_init(reverb_settings *s){
  memset(s,0,sizeof(*s));
  s->liveness=500;
  s->hfdamp=500;
  s->wet=1000;
  s->width=1000;
  s->delay=0;
  s->roomsize=1000;
}

/* ri: pointer to array of instances
   rs: pointer to array of settings 

   return processing status: -) input error
                             0) pre-stream
                             1) processing
                             2) eos 

   array sizes must match number of channels in in/outL/outR */

extern int input_size;

int reverb_process(reverb_instance *ri, reverb_settings **rs,
		   time_linkage *in, time_linkage *outL, time_linkage *outR){
  
  int ch=ri->ch,i;
  u_int32_t active=0;

  /* verify input consistency */
  //if(ri->blocksize != in->blocksize) return -1;
  //if(ri->blocksize != outL->blocksize) return -1;
  //if(outR && ri->blocksize != outR->blocksize) return -1;
  if(ch != in->channels) return -1;
  if(ch != outL->channels) return -1;
  if(outR && ch != outR->channels) return -1;
  if(ri->reverbs->rR && !outR) return -1;
  if(!ri->reverbs->rR && outR) return -1;

  switch(ri->initstate){
  case 0:
    
    if(in->samples==0){
      /* clear output, return pre stream status */
      if(outL)time_linkage_clear(outL);
      if(outR)time_linkage_clear(outR);
      
      return 0;
    }
    
    ri->initstate=1; 
    /* fall through */
  case 1:

    if(in->samples>0){
      for(i=0;i<ch;i++){
	reverb_instance_one *rio=ri->reverbs+i;
	float *tmp=rio->cache;

	u_int32_t ret=process_one(rio, rs[i], in->data[i], mute_channel_muted(in->active,i),
				  outL->data[i],(outR?outR->data[i]:0),ri->transwindow,
				  in->samples,ri->blocksize);
	
	if(rs[i]->dry_mix && !mute_channel_muted(in->active,i)){
	  int j;
	  float *dataO=outL->data[i];
	  float *dataI=in->data[i];
	  
	  if(ret){
	    for(j=0;j<in->samples;j++)
	      dataO[j]+=dataI[j];
	    
	    if(outR){
	      dataO=outR->data[j];
	      for(j=0;j<in->samples;j++)
		dataO[j]+=dataI[j];
	    }
	  }else{
	    for(j=0;j<in->samples;j++)
	      dataO[j]=dataI[j];
	    
	    if(outR){
	      dataO=outR->data[j];
	      for(j=0;j<in->samples;j++)
		dataO[j]=dataI[j];
	    }
	  }
	  
	  active |= (1<<i);
	  
	}
	rio->cache=in->data[i];
	in->data[i]=tmp;
	active|=(ret<<i);
      }
      if(outL){
	outL->samples=in->samples;
	outL->active=active;
      }
      if(outR){
	outR->samples=in->samples;
	outR->active=active;
      }
      if(in->samples<input_size)ri->initstate=2;
      return 1;
    }
    ri->initstate=2;
    /* fall through */
  case 2:
    if(outL)time_linkage_clear(outL);
    if(outR)time_linkage_clear(outR);    
    return 2;
  }
  return -1;
}

int reverb_instance_init(reverb_instance *ri, int ch, int stereo){
  int i;
  memset(ri,0,sizeof(*ri));

  ri->ch=ch;
  ri->blocksize=input_size;
  ri->transwindow=window_get(1,ri->blocksize);
  ri->reverbs=malloc(ch*sizeof(*ri->reverbs));

  for(i=0;i<ch;i++){
    reverb_instance_one *rio=ri->reverbs+i;
    
    rio->cache=malloc(ri->blocksize*sizeof(*rio->cache));
    rio->rL=malloc(sizeof(*rio->rL));
    rio->rR=0;
    if(stereo) rio->rR=malloc(sizeof(*rio->rR));

  }
  
  reverb_instance_reset(ri);
  return 0;
}

/* Old postfish hooks; these will be removed next step when
   libpostfish appears */

extern int input_ch;
static reverb_instance cs;
static reverb_instance ms;
reverb_settings *reverb_channelset; 
reverb_settings reverb_masterset; 

static reverb_settings **rcs;
static reverb_settings **rms;

static time_linkage outA;
static time_linkage outB;
static time_linkage outC;
static time_linkage outM;

int p_reverb_load(void){
  int i;

  reverb_instance_init(&cs,input_ch,1);
  reverb_instance_init(&ms,OUTPUT_CHANNELS,0);

  reverb_channelset=malloc(input_ch*sizeof(*reverb_channelset));
  for(i=0;i<input_ch;i++)
    reverb_settings_init(reverb_channelset+i);
  reverb_settings_init(&reverb_masterset);

  rcs=malloc(input_ch*sizeof(*rcs));
  for(i=0;i<input_ch;i++)
    rcs[i]=reverb_channelset+i;
  rms=malloc(OUTPUT_CHANNELS*sizeof(*rms));
  for(i=0;i<input_ch;i++)
    rms[i]=&reverb_masterset;

  time_linkage_init(&outA,input_ch);
  time_linkage_init(&outB,input_ch);
  time_linkage_init(&outC,input_ch);
  time_linkage_init(&outM,OUTPUT_CHANNELS);
  
  reverb_masterset.dry_mix=1;

  return 0;
}

void p_reverb_reset(void){
  reverb_instance_reset(&cs);
  reverb_instance_reset(&ms);
}
 
time_linkage *p_reverb_read_channel(time_linkage *in,
				    time_linkage **revA,
				    time_linkage **revB){
  
  time_linkage_copy(&outC,in);
  reverb_process(&cs,rcs,in,&outA,&outB);
  *revA=&outA;
  *revB=&outB;
  return &outC;
}

time_linkage *p_reverb_read_master(time_linkage *in){  
  reverb_process(&ms,rms,in,&outM,0);
  return &outM;
}

