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
#include "multicompand.h"
#include <fftw3.h>
#include "subband.h"
#include "bessel.h"

static int offset=0;
static void _analysis(char *base,int i,float *v,int n,int bark,int dB){
  int j;
  FILE *of;
  char buffer[80];

  sprintf(buffer,"%s_%d.m",base,i);
  of=fopen(buffer,"w");
  
  if(!of)perror("failed to open data dump file");
  
  for(j=0;j<n;j++){
    if(bark){
      float b=toBark((4000.f*j/n)+.25);
      fprintf(of,"%f ",b);
    }else
      fprintf(of,"%f ",(float)j);
    
    if(dB){
      float val=todB(hypot(v[j],v[j+1]));
      if(val<-140)val=-140;
      fprintf(of,"%f\n",val);
      j++;
     
    }else{
      fprintf(of,"%f\n",v[j]);
    }
  }
  fclose(of);
}

static void _analysis_append(char *base,int basemod,float *v,int n,int off){
  int j;
  FILE *of;
  char buffer[80];

  sprintf(buffer,"%s_%d.m",base,basemod);
  of=fopen(buffer,"a");
  
  if(!of)perror("failed to open data dump file");
  
  for(j=0;j<n;j++){
    fprintf(of,"%f ",(float)j+off);    
    fprintf(of,"%f\n",v[j]);
  }
  fprintf(of,"\n");
  fclose(of);
}

extern int input_size;
extern int input_rate;
extern int input_ch;

/* lookup table this soon */
static float soft_knee(float x){
  return atanf(-x*.2)*x/3.14159-x*.5-(5./M_PI);
}

static float hard_knee(float x){
  return (x>0?-x:0.);
}

typedef struct {
  double c0;
  double c1;
  double g;
  float alpha; 
  //int impulseahead;  //  5764 == 1Hz @ 44.1
  //int stepahead;     // 14850 == 1Hz @ 44.1
} iir_filter;

static inline long impulse_ahead(float alpha){
  return rint(.1307f/alpha);
}

static inline long step_ahead(float alpha){
  return rint(.3367f/alpha);
}

static inline float step_freq(long ahead){
  return input_rate*.3367/ahead;
}

#if NASTY_IEEE_FLOAT32_HACK_IS_FASTER_THAN_LOG

#define todB_p(x) (((*(int32_t*)(x)) & 0x7fffffff) * 7.1771144e-7f - 764.27118f)

#else

#define todB_p(x) todB(*(x))

#endif

typedef struct {
  double x[2];
  double y[2];
  int state;
} iir_state;

typedef struct {
  subband_state ss;
  subband_window sw[multicomp_banks];
  
  iir_filter over_attack[multicomp_banks][multicomp_freqs_max];
  iir_filter over_decay[multicomp_banks][multicomp_freqs_max];

  iir_filter under_attack[multicomp_banks][multicomp_freqs_max];
  iir_filter under_decay[multicomp_banks][multicomp_freqs_max];

  iir_filter suppress_attack[multicomp_banks][multicomp_freqs_max];
  iir_filter suppress_decay[multicomp_banks][multicomp_freqs_max];
  iir_filter suppress_release[multicomp_banks][multicomp_freqs_max];

  iir_state *over_iir[multicomp_freqs_max];
  iir_state *under_iir[multicomp_freqs_max];
  iir_state *suppress_iirA[multicomp_freqs_max];
  iir_state *suppress_iirB[multicomp_freqs_max];

  sig_atomic_t pending_bank;
  sig_atomic_t active_bank;

  int previous_over_mode; // RMS or peak?  The iir follower domains
  // are different, and upon transition, must be converted.
  int previous_under_mode;    // as above
  int previous_suppress_mode; // as above

} multicompand_state;

sig_atomic_t compand_visible;
sig_atomic_t compand_active;

banked_compand_settings bc[multicomp_banks];
other_compand_settings c;
multicompand_state ms;

int pull_multicompand_feedback(float **peak,float **rms,int *bands){
  return pull_subband_feedback(&ms.ss,peak,rms,bands);
}

static sig_atomic_t pending_bank=0;
static sig_atomic_t reading=0;
void multicompand_reset(){
  int h,i,j;
  
  subband_reset(&ms.ss);
  
  for(i=0;i<multicomp_freqs_max;i++)
    for(j=0;j<input_ch;j++){
      memset(&ms.over_iir[i][j],0,sizeof(iir_state));
      memset(&ms.under_iir[i][j],0,sizeof(iir_state));
      memset(&ms.suppress_iirA[i][j],0,sizeof(iir_state));
      memset(&ms.suppress_iirB[i][j],0,sizeof(iir_state));
    }
}

int multicompand_load(void){
  int h,i,j;
  int qblocksize=input_size/8;
  memset(&ms,0,sizeof(ms));

  subband_load(&ms.ss,multicomp_freqs_max,qblocksize);

  for(h=0;h<multicomp_banks;h++)
    subband_load_freqs(&ms.ss,&ms.sw[h],multicomp_freq_list[h],
		       multicomp_freqs[h]);
    
  for(i=0;i<multicomp_freqs_max;i++){
    ms.over_iir[i]=calloc(input_ch,sizeof(iir_state));
    ms.under_iir[i]=calloc(input_ch,sizeof(iir_state));
    ms.suppress_iirA[i]=calloc(input_ch,sizeof(iir_state));
    ms.suppress_iirB[i]=calloc(input_ch,sizeof(iir_state));
  }

  ms.active_bank=0;

  return 0;
}

int multicompand_filterbank_set(float msec,iir_filter filterbank[multicomp_banks][multicomp_freqs_max]){
  int i,j;
  for(j=0;j<multicomp_banks;j++){
    float *freqs=multicomp_freq_list[j];
    int bands=multicomp_freqs[j];
    iir_filter *filters=filterbank[j];
    
    for(i=0;i<bands;i++){
      float alpha;
      float corner_freq=500./msec;

      /* limit the filter corner frequency to prevent fast attacks
         from destroying low frequencies */
      if(corner_freq>freqs[i]/2)corner_freq=freqs[i]/2;
      
      /* make sure the chosen frequency doesn't require a lookahead
         greater than what's available */
      if(step_freq(input_size*2-ms.ss.qblocksize*3)*.95<freqs[i])
	corner_freq=step_freq(input_size*2-ms.ss.qblocksize*3);
		     
      alpha=corner_freq/input_rate*2.;
      filters[i].g=mkbessel_2(alpha,&filters[i].c0,&filters[i].c1);
      filters[i].alpha=alpha;
    }
  }
}


int multicompand_over_attack_set(float msec){
  multicompand_filterbank_set(msec,ms.over_attack);
}

int multicompand_over_decay_set(float msec){
  multicompand_filterbank_set(msec,ms.over_decay);  
}

int multicompand_under_attack_set(float msec){
  multicompand_filterbank_set(msec,ms.under_attack);
}

int multicompand_under_decay_set(float msec){
  multicompand_filterbank_set(msec,ms.under_decay);
}

int multicompand_suppress_attack_set(float msec){
  multicompand_filterbank_set(msec,ms.suppress_attack);
}

int multicompand_suppress_decay_set(float msec){
  multicompand_filterbank_set(msec,ms.suppress_decay);
}

int multicompand_suppress_release_set(float msec){
  multicompand_filterbank_set(msec,ms.suppress_release);
}

/* assymetrical attack/decay filter; the z-space fixup is designed for
   the case where decay is the same or slower than attack */
/* The Bessel filter followers are inlined here as later to avoid some
   really shockingly bad code generation by gcc on x86 */
static void compute_iir(float *y, float *x, int n,
			iir_state *is, iir_filter *attack, iir_filter *decay){
  double a_c0=attack->c0;
  double d_c0=decay->c0;
  double a_c1=attack->c1;
  double d_c1=decay->c1;
  double a_g=attack->g;
  double d_g=decay->g;
  
  double x0=is->x[0];
  double x1=is->x[1];
  double y0=is->y[0];
  double y1=is->y[1];
  int state=is->state;
    
  int i=0;

  if(x[0]>y0)state=0; 
      
  while(i<n){
    
    if(state==0){
      /* attack case */
      while(i<n){
	y[i] = (x[i]+x0*2.+x1)/a_g + y0*a_c0+y1*a_c1;
    
	if(y[i]<y0){
	  /* decay fixup; needed because we're in discontinuous time.  In a
	     physical time-assymmetric Bessel follower, the decay case would
	     take over at the instant the attack filter peaks.  In z-space,
	     the attack filter has already begun to decay, and the decay
	     filter otherwise takes over with an already substantial
	     negative slope in the filter state, or if it takes over in the
	     preceeding time quanta, a substantial positive slope.  Fix this
	     up to take over at zero y slope. */
	  y1=y0;
	  state=1; 
	  break;
	}
	x1=x0;x0=x[i];
	y1=y0;y0=y[i];
	i++;
      }
    }

    if(state==1){
      /* decay case */
      while(1){
	y[i] = (x[i]+x0*2+x1)/d_g + y0*d_c0+y1*d_c1;

	x1=x0;x0=x[i];
	y1=y0;y0=y[i];
	i++;

	if(i>=n)break;
	if(x[i]>y0){
	  state=0;
	  break;
	}
      }
    }
  }
  
  is->x[0]=x0;
  is->x[1]=x1;
  is->y[0]=y0;
  is->y[1]=y1;
  is->state=state;
  
}

static void prepare_rms(float *rms, float **xx, int n, int ch,int ahead){
  if(ch){
    int i,j;

    memset(rms,0,sizeof(*rms)*n);
    
    for(j=0;j<ch;j++){
      float *x=xx[j]+ahead;
      for(i=0;i<n;i++)
	rms[i]+=x[i]*x[i];
    }
    if(ch>1)
      for(i=0;i<n;i++)
	rms[i]/=ch;
  }
}

static void prepare_peak(float *peak, float **xx, int n, int ch,int ahead){
  if(ch){
    int j,k,l;

    memset(peak,0,sizeof(*peak)*n);

    for(l=0;l<ch;l++){
      
      float *x=xx[l];
      int ii,jj;
      int loc=0;
      float val=fabs(x[0]);
      
      /* find highest point in next [ahead] */
      for(ii=1;ii<ahead;ii++)
	if(fabs(x[ii])>val){
	  val=fabs(x[ii]);
	  loc=ii;
	}
      if(val>peak[0])peak[0]=val;
      
      for(ii=1;ii<n;ii++){
	if(fabs(x[ii+ahead])>val){
	  val=fabs(x[ii+ahead]);
	  loc=ii+ahead;
	}	  
	if(ii>=loc){
	  /* backfill */
	  val=0;
	  for(jj=ii+ahead-1;jj>=ii;jj--){
	    if(fabs(x[jj])>val)val=fabs(x[jj]);
	    if(jj<n && val>peak[jj])peak[jj]=val;
	  }
	  val=fabs(x[ii+ahead-1]);
	  loc=ii+ahead;
	}
	if(val>peak[ii])peak[ii]=val; 
      }
    }
    
    for(j=0;j<n;j++)
      peak[j]=todB_p(peak+j);
  }
}

static void run_filter_only(float *dB,float *work,float **x,int n,int mode,
			    iir_state *iir,iir_filter *attack,iir_filter *decay){
  int i;

  compute_iir(dB, work, n, &iir[i], attack, decay);

  if(mode==0)
    for(i=0;i<n;i++)
      dB[i]=todB_p(dB+i)*.5f;
}

static void run_filter(float *dB,float *work,float **x,int n, int ch, int i,
		       float lookahead,int link,int mode,
		       iir_state *iir,iir_filter *attack,iir_filter *decay){
  if(link){
    if(i==0){
      if(mode)
	prepare_peak(work, x, n, ch, step_ahead(attack->alpha)*lookahead);

      else
	prepare_rms(work, x, n, ch, impulse_ahead(attack->alpha)*lookahead);

    }
  }else{
    if(mode)
      prepare_peak(work, x+i, n, 1, step_ahead(attack->alpha)*lookahead);
    else
      prepare_rms(work, x+i, n, 1, impulse_ahead(attack->alpha)*lookahead);
  }

  run_filter_only(dB,work,x,n,mode,iir,attack,decay);
}

static void over_compand(float ***x){
  int i,j,k;
  float work[input_size];
  float dB[input_size];
  float lookahead=c.over_lookahead/1000.;
  int link=c.link_mode;
  int mode=c.over_mode;

  float (*knee)(float)=(c.over_softknee?soft_knee:hard_knee);
  
  float corner_multiplier=(1.-1./(c.over_ratio/1000.));
  float base_multiplier=(1.-1./(c.base_ratio/1000.));

  if(ms.previous_over_mode!=mode){
    /* the followers for RMS are in linear^2 mode, the followers for
       peak are dB.  If the mode has changed, we need to convert from
       one to the other to avoid pops */
    if(mode==0){
      /* from dB to linear^2 */
      for(i=0;i<multicomp_freqs_max;i++){
	for(j=0;j<input_ch;j++){
	  ms.over_iir[i][j].x[0]=fromdB(ms.over_iir[i][j].x[0]*2.);
	  ms.over_iir[i][j].x[1]=fromdB(ms.over_iir[i][j].x[1]*2.);
	  ms.over_iir[i][j].y[0]=fromdB(ms.over_iir[i][j].y[0]*2.);
	  ms.over_iir[i][j].y[1]=fromdB(ms.over_iir[i][j].y[1]*2.);
	}
      }
    }else{
      /* from linear^2 to dB */
      for(i=0;i<multicomp_freqs_max;i++){
	for(j=0;j<input_ch;j++){
	  ms.over_iir[i][j].x[0]=todB(ms.over_iir[i][j].x[0])*.5;
	  ms.over_iir[i][j].x[1]=todB(ms.over_iir[i][j].x[1])*.5;
	  ms.over_iir[i][j].y[0]=todB(ms.over_iir[i][j].y[0])*.5;
	  ms.over_iir[i][j].y[1]=todB(ms.over_iir[i][j].y[1])*.5;
	}
      }
    }
  }

  ms.previous_over_mode=mode;

  for(i=0;i<multicomp_freqs[ms.active_bank];i++){
    float zerocorner=bc[ms.active_bank].static_o[i]/10.;
    float limitcorner=zerocorner + fabs((c.over_limit*10.)/corner_multiplier);

    for(j=0;j<input_ch;j++){
      float *lx=x[i][j];
      run_filter(dB,work,x[i],input_size,input_ch,j,lookahead,link,mode,
		 ms.over_iir[i],&ms.over_attack[ms.active_bank][i],&ms.over_decay[ms.active_bank][i]);

      if(compand_active)
	for(k=0;k<input_size;k++){
	  float adj=(knee(dB[i]-zerocorner)-knee(dB[i]-limitcorner))*corner_multiplier;
	  adj-=  (dB[i]+adj)*base_multiplier;
	  lx[k]*=fromdB(adj);
	}
    }
  }
}

static void under_compand(float ***x){
  int i,j,k;
  float work[input_size];
  float dB[input_size];
  float lookahead=c.under_lookahead/1000.;
  int link=c.link_mode;
  int mode=c.under_mode;

  float (*knee)(float)=(c.under_softknee?soft_knee:hard_knee);
  
  float corner_multiplier=(1.-1./(c.under_ratio/1000.));

  if(ms.previous_under_mode!=mode){
    /* the followers for RMS are in linear^2 mode, the followers for
       peak are dB.  If the mode has changed, we need to convert from
       one to the other to avoid pops */
    if(mode==0){
      /* from dB to linear^2 */
      for(i=0;i<multicomp_freqs_max;i++){
	for(j=0;j<input_ch;j++){
	  ms.under_iir[i][j].x[0]=fromdB(ms.under_iir[i][j].x[0]*2.);
	  ms.under_iir[i][j].x[1]=fromdB(ms.under_iir[i][j].x[1]*2.);
	  ms.under_iir[i][j].y[0]=fromdB(ms.under_iir[i][j].y[0]*2.);
	  ms.under_iir[i][j].y[1]=fromdB(ms.under_iir[i][j].y[1]*2.);
	}
      }
    }else{
      /* from linear^2 to dB */
      for(i=0;i<multicomp_freqs_max;i++){
	for(j=0;j<input_ch;j++){
	  ms.under_iir[i][j].x[0]=todB(ms.under_iir[i][j].x[0])*.5;
	  ms.under_iir[i][j].x[1]=todB(ms.under_iir[i][j].x[1])*.5;
	  ms.under_iir[i][j].y[0]=todB(ms.under_iir[i][j].y[0])*.5;
	  ms.under_iir[i][j].y[1]=todB(ms.under_iir[i][j].y[1])*.5;
	}
      }
    }
  }

  ms.previous_under_mode=mode;

  for(i=0;i<multicomp_freqs[ms.active_bank];i++){
    float zerocorner=bc[ms.active_bank].static_o[i]/10.;
    float limitcorner=zerocorner- fabs((c.under_limit*10.)/corner_multiplier);

    for(j=0;j<input_ch;j++){
      float *lx=x[i][j];
      run_filter(dB,work,x[i],input_size,input_ch,j,lookahead,link,mode,
		 ms.under_iir[i],&ms.under_attack[ms.active_bank][i],&ms.under_decay[ms.active_bank][i]);
      if(compand_active)
	for(k=0;k<input_size;k++)
	  lx[k]*=fromdB((knee(zerocorner-dB[i])-knee(limitcorner-dB[i]))*corner_multiplier);
    }
  }
}

  /* (since this one is kinda unique) The Suppressor....

     Reverberation in a measurably live environment displays
     log amplitude decay with time (linear decay when plotted on a dB
     scale).

     In its simplest form, the suppressor follows actual {RMS|peak}
     amplitude attacks but chooses a slower-than-actual decay, then
     expands according to the dB distance between the slow and actual
     decay.

     The 'depth' setting is used to limit the expanded distance
     between actual and slow decay; it's also used to drag the slow
     decay down with the actual decay once the expansion has hit the
     depth limit.

     Thus, the suppressor can be used to 'dry out' a very 'wet'
     reverberative track. */

static void suppress(float ***x){
  int i,j,k;
  float work[input_size];
  float dB_fast[input_size];
  float dB_slow[input_size];
  int link=c.link_mode;
  int mode=c.suppress_mode;

  float multiplier=(1.-1./(c.suppress_ratio/1000.));

  if(ms.previous_suppress_mode!=mode){
    /* the followers for RMS are in linear^2 mode, the followers for
       peak are dB.  If the mode has changed, we need to convert from
       one to the other to avoid pops */
    if(mode==0){
      /* from dB to linear^2 */
      for(i=0;i<multicomp_freqs_max;i++){
	for(j=0;j<input_ch;j++){
	  ms.suppress_iirA[i][j].x[0]=fromdB(ms.suppress_iirA[i][j].x[0]*2.);
	  ms.suppress_iirA[i][j].x[1]=fromdB(ms.suppress_iirA[i][j].x[1]*2.);
	  ms.suppress_iirA[i][j].y[0]=fromdB(ms.suppress_iirA[i][j].y[0]*2.);
	  ms.suppress_iirA[i][j].y[1]=fromdB(ms.suppress_iirA[i][j].y[1]*2.);
	  ms.suppress_iirB[i][j].x[0]=fromdB(ms.suppress_iirB[i][j].x[0]*2.);
	  ms.suppress_iirB[i][j].x[1]=fromdB(ms.suppress_iirB[i][j].x[1]*2.);
	  ms.suppress_iirB[i][j].y[0]=fromdB(ms.suppress_iirB[i][j].y[0]*2.);
	  ms.suppress_iirB[i][j].y[1]=fromdB(ms.suppress_iirB[i][j].y[1]*2.);
	}
      }
    }else{
      /* from linear^2 to dB */
      for(i=0;i<multicomp_freqs_max;i++){
	for(j=0;j<input_ch;j++){
	  ms.suppress_iirA[i][j].x[0]=todB(ms.suppress_iirA[i][j].x[0])*.5;
	  ms.suppress_iirA[i][j].x[1]=todB(ms.suppress_iirA[i][j].x[1])*.5;
	  ms.suppress_iirA[i][j].y[0]=todB(ms.suppress_iirA[i][j].y[0])*.5;
	  ms.suppress_iirA[i][j].y[1]=todB(ms.suppress_iirA[i][j].y[1])*.5;
	  ms.suppress_iirB[i][j].x[0]=todB(ms.suppress_iirB[i][j].x[0])*.5;
	  ms.suppress_iirB[i][j].x[1]=todB(ms.suppress_iirB[i][j].x[1])*.5;
	  ms.suppress_iirB[i][j].y[0]=todB(ms.suppress_iirB[i][j].y[0])*.5;
	  ms.suppress_iirB[i][j].y[1]=todB(ms.suppress_iirB[i][j].y[1])*.5;
	}
      }
    }
  }

  ms.previous_suppress_mode=mode;

  for(i=0;i<multicomp_freqs[ms.active_bank];i++){
    for(j=0;j<input_ch;j++){
      float *lx=x[i][j];

      run_filter(dB_fast,work,x[i],input_size,input_ch,j,1.,link,mode,
		 ms.suppress_iirA[i],&ms.suppress_attack[ms.active_bank][i],&ms.suppress_decay[ms.active_bank][i]);
      run_filter_only(dB_slow,work,x[i],input_size,mode,
		      ms.suppress_iirB[i],&ms.suppress_attack[ms.active_bank][i],&ms.suppress_release[ms.active_bank][i]);
      
      if(compand_active && multiplier!=0.)
	for(k=0;k<input_size;k++){
	  float adj=(dB_fast[i]-dB_slow[i])*multiplier;  
	  lx[k]*=fromdB(adj);
	}
    }
  }
}

static void multicompand_work(float **peakfeed,float **rmsfeed){
  int i,j,k;

  under_compand(ms.ss.lap);
  over_compand(ms.ss.lap);

  /* feedback displays the results of the static compander */
  if(c.link_mode==0){
    for(i=0;i<multicomp_freqs[ms.active_bank];i++){
      for(j=0;j<input_ch;j++){
	float *x=ms.ss.lap[i][j];
	float rms=0,peak=0;
	
	for(k=0;k<input_size;k++){
	  rms+=x[k]*x[k];
	  if(fabs(x[k])>peak)peak=fabs(x[k]);
	}	    
	
	peakfeed[i][j]=todB(peak);
	rmsfeed[i][j]=todB(rms/input_size)*.5;
      }
    }
  }else{
    for(i=0;i<multicomp_freqs[ms.active_bank];i++){
      float rms=0,peak=0;
      for(j=0;j<input_ch;j++){
	float *x=ms.ss.lap[i][j];
	
	for(k=0;k<input_size;k++){
	  rms+=x[k]*x[k];
	  if(fabs(x[k])>peak)peak=fabs(x[k]);
	}	
      }    
	
      for(j=0;j<input_ch;j++){
	peakfeed[i][j]=todB(peak);
	rmsfeed[i][j]=todB(rms/input_size/input_ch)*.5;
      }
    }
  }

  suppress(ms.ss.lap);
  
  offset+=input_size;
}

void multicompand_set_bank(int bank){
  ms.pending_bank=bank;
}

static int bypass_state;
static int active_state;
time_linkage *multicompand_read(time_linkage *in){
  int i,j;
  int pending=ms.pending_bank;
  int bypass=!(compand_visible||compand_active);
  
  if(pending!=ms.active_bank || (bypass && !bypass_state)){

  }
   
  ms.active_bank=ms.pending_bank;
  bypass_state=bypass;

  return subband_read(in,&ms.ss,&ms.sw[ms.active_bank],
		      multicompand_work,bypass);
}


