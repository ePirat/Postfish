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

/* An array of two tap Bessel filters for bandlimiting our followers
   and performing continuous RMS estimation.  Why Bessel filters?  Two
   reasons: They exhibit very little ringing in the time domain and
   about a half-order of magnitude less roundoff noise from gain
   (compared to other IIR families) */

static float iirg_list[8]={
  4.875178475e+06,  // 5
  1.008216556e+06,  // 11
  2.524889925e+05,  // 22
  6.3053949794e+04, // 44
  1.5872282930e+04, // 88
  4.086422543e+03,  // 175
  1.049342702e+03,  // 350
  2.764099277e+02   // 700
};

static float iirc_list[8][2]={
  {1.9984308946, -.9984317150},  // 5
  {1.9965490514, -.9965530188},  // 11
  {1.9931020727, -.9931179150},  // 22
  {1.9861887623, -.98625220002}, // 44
  {1.9724411246, -.97269313627}, // 88
  {1.9455669653, -.9465458165},  // 175
  {1.8921217885, -.8959336985},  // 350
  {1.7881166933, -0.8025879536}  // 700
};

static float iir_corner_list[8]={5,11,22,44,88,175,350,700};
static float iir_rmsoffset_list[8]={1048,524,262,131,65,32,16,8};
static float iir_peakoffset_list[8]={5400,2700,1350,675,338,169,84,42};

#if NASTY_IEEE_FLOAT32_HACK_IS_FASTER_THAN_LOG

#define todB_p(x) (((*(int32_t*)(x)) & 0x7fffffff) * 7.1771144e-7f - 764.27118f)

#else

#define todB_p(x) todB(*(x))

#endif

typedef struct {
  /* RMS follower state */
  float rms_smooth_y[2];
  float rms_smooth_x[2];

  float static_c_decay_chase;
  float static_e_decay_chase;
  float static_g_decay_chase;
  float suppress_decay_chase;

  float env1_x[2];
  float env1_y[2];
  float env2_x[2];
  float env2_y[2];

  int dummymarker;
} envelope_state;

typedef struct {
  subband_state ss;
  subband_window sw[multicomp_banks];

  int rmschoice[multicomp_banks][multicomp_freqs_max];
  int attack[multicomp_banks][multicomp_freqs_max];
  int decay[multicomp_banks][multicomp_freqs_max];

  envelope_state **e;
  sig_atomic_t pending_bank;
  sig_atomic_t active_bank;
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

  for(i=0;i<input_ch;i++)
    for(j=0;j<multicomp_freqs_max;j++){
      memset(&ms.e[j][i],0,sizeof(**ms.e));
      ms.e[j][i].static_c_decay_chase=0.;
      ms.e[j][i].static_e_decay_chase=0.;
      ms.e[j][i].static_g_decay_chase=0.;
    }

}

int multicompand_load(void){
  int h,i,j;
  int qblocksize=input_size/8;
  memset(&ms,0,sizeof(ms));

  subband_load(&ms.ss,multicomp_freqs_max,qblocksize);

  for(h=0;h<multicomp_banks;h++){
    subband_load_freqs(&ms.ss,&ms.sw[h],multicomp_freq_list[h],
		       multicomp_freqs[h]);
    
    /* select the RMS follower filters to use according to 
       sample rate and band frequency */
    for(i=0;i<multicomp_freqs[h];i++){
      /* select a filter with a corner frequency of about 1/8 the
	 band center. Don't go lower than 11 or higher than the 44 */
      /* also... make sure 'workahead' is within input size restrictions */
      double bf=multicomp_freq_list[h][i];
      
      for(j=7;j>=0;j--)
	if(iir_peakoffset_list[j]<=input_size*2-qblocksize*3){
	  ms.rmschoice[h][i]=j;
	  if(iir_corner_list[j]*input_rate/44100 < bf*.25 &&
	    iir_corner_list[j]*input_rate/44100 < 100)
	    break;
	}

      for(j=7;j>=0;j--)
	if(iir_peakoffset_list[j]<=input_size*2-qblocksize*3){
	  ms.attack[h][i]=j;
	  if(iir_corner_list[j]*input_rate/44100 < bf*.5)
	    break;
	}
      ms.decay[h][i]=j;
      for(j=7;j>=0;j--)
	if(iir_peakoffset_list[j]<=input_size*2-qblocksize*3){
	  ms.decay[h][i]=j;
	  if(iir_corner_list[j]*input_rate/44100 < 30 &&
	     j<=ms.attack[h][i])
	    break;
	}
    }
  }
  
  ms.e=calloc(multicomp_freqs_max,sizeof(*(ms.e)));
  
  for(i=0;i<multicomp_freqs_max;i++){
    ms.e[i]=calloc(input_ch,sizeof(**(ms.e)));
    
    for(j=0;j<input_ch;j++){
      ms.e[i][j].static_c_decay_chase=0.;
      ms.e[i][j].static_e_decay_chase=0.;
      ms.e[i][j].static_g_decay_chase=0.;
    }
  }
  
  ms.active_bank=0;

  return 0;
}

/* The Bessel filter followers are inlined and unrolled here as later
   to avoid some really shockingly bad code generation by gcc on x86 */
static void rms_chase(float *rms, int i, int linkp, int filternum,
		      envelope_state *e, float **xxi){
  int k,l;
  float iirc0,iirc1;
  float iirg,x,y;
  float iirx0,iirx1;
  float iiry0,iiry1;

  if(linkp==0 || i==0){
    float val;
    int ahead=iir_rmsoffset_list[filternum];
    iirg=iirg_list[filternum];
    iirc0=iirc_list[filternum][0];
    iirc1=iirc_list[filternum][1];
    if(linkp)memset(rms,0,sizeof(*rms)*input_size);
    
    for(l=i;l<(linkp?input_ch:i+1);l++){
      float *xx=xxi[l];
      iirx0=e[l].rms_smooth_x[0];
      iirx1=e[l].rms_smooth_x[1];
      iiry0=e[l].rms_smooth_y[0];
      iiry1=e[l].rms_smooth_y[1];
      
      for(k=0;k<input_size;k+=2){
	y=x=(xx[k+ahead]*xx[k+ahead])/iirg;
	y+=iirx0;
	y+=iirx0;
	y+=iirx1;
	y+=iiry0*iirc0;
	y+=iiry1*iirc1;
	
	iirx1=x;
	rms[k]=iiry1=y;
	
	y=x=(xx[k+ahead+1]*xx[k+ahead+1])/iirg;
	y+=iirx1;
	y+=iirx1;
	y+=iirx0;
	y+=iiry1*iirc0;
	y+=iiry0*iirc1;
	
	iirx0=x;
	rms[k+1]=iiry0=y;
      }

      e[l].rms_smooth_x[0]=iirx0;
      e[l].rms_smooth_x[1]=iirx1;
      e[l].rms_smooth_y[0]=iiry0;
      e[l].rms_smooth_y[1]=iiry1;
      
    }
    
    {
      float scale=(linkp?1./input_ch:1);
      for(k=0;k<input_size;k++){
	val=fabs(rms[k]*scale);
	rms[k]=sqrt(val);
      }
    } 

    for(k=0;k<input_size;k++)
      rms[k]=todB_p(rms+k);
    
  }
}

static void peak_chase(float *peak, int i, int linkp, int filternum,
		       float **xxi){
  int l;
  if(linkp==0 || i==0){
    memset(peak,0,sizeof(*peak)*input_size);
    for(l=i;l<(linkp?input_ch:i+1);l++){
      
      float *x=xxi[l];
      int ii,jj;
      int loc=0;
      float val=fabs(x[0]);
      int ahead=iir_peakoffset_list[filternum];
      
      /* find highest point in next [ahead] */
      for(ii=1;ii<ahead;ii++)
	if(fabs(x[ii])>val){
	  val=fabs(x[ii]);
	  loc=ii;
	}
      if(val>peak[0])peak[0]=val;
      
      for(ii=1;ii<input_size;ii++){
	if(fabs(x[ii+ahead])>val){
	  val=fabs(x[ii+ahead]);
	  loc=ii+ahead;
	}	  
	if(ii>=loc){
	  /* backfill */
	  val=0;
	  for(jj=ii+ahead-1;jj>=ii;jj--){
	    if(fabs(x[jj])>val)val=fabs(x[jj]);
	    if(jj<input_size && val>peak[jj])peak[jj]=val;
	  }
	  val=fabs(x[ii+ahead-1]);
	  loc=ii+ahead;
	}
	if(val>peak[ii])peak[ii]=val; 
      }
      
      for(ii=0;ii<input_size;ii++)
	peak[ii]=todB_p(peak+ii);
    }
  }
}

static void static_compand(float *peak, float *rms, float *gain, 
			   float gate,float expand, float compress,
			   envelope_state *e){
  int ii;
  float current_c=e->static_c_decay_chase;
  float current_e=e->static_e_decay_chase;
  float current_g=e->static_g_decay_chase;

  float decay_c=c.static_c_decay/(float)(1024*1024);
  float decay_e=c.static_e_decay/(float)(1024*1024);
  float decay_g=c.static_g_decay/(float)(1024*1024);

  float *envelope=rms;
  
  float ratio_c=100./c.static_c_ratio;
  float ratio_e=c.static_e_ratio/100.;
  
  if(c.static_mode)envelope=peak;
  
  for(ii=0;ii<input_size;ii++){
    float adj=0.,local_adj;
    float current=envelope[ii];
    
    /* compressor */
    local_adj=0.;
    if(current>compress){
      float current_over=current-compress;
      float compress_over=current_over*ratio_c;
      local_adj=compress_over-current_over;
    }
    /* compressor decay is related to how quickly we release
       attenuation */
    current_c-=decay_c;
    if(local_adj<current_c)current_c=local_adj;
    adj+=current_c;

    /* expander */
    local_adj=0.;
    if(current<expand){
      float current_under=expand-current;
      float expand_under=current_under*ratio_e;
      local_adj= current_under-expand_under;
    }
    /* expander decay is related to how quickly we engage
       attenuation */
    current_e+=decay_e;
    if(local_adj>current_e)current_e=local_adj;
    adj+=current_e;

    /* gate */
    local_adj=0.;
    if(current<gate){
      float current_under=gate-current;
      float gate_under=current_under*10.;
      local_adj= current_under-gate_under;
    }
    /* gate decay is related to how quickly we engage
       attenuation */
    current_g+=decay_g;
    if(local_adj>current_g)current_g=local_adj;
    adj+=current_g;

    
    if(adj<-150.)adj=-150;
    gain[ii]=adj;
  }
  
  e->static_c_decay_chase=current_c;
  e->static_e_decay_chase=current_e;
  e->static_g_decay_chase=current_g;
  
}

static void range_compand(float *peak, float *rms, float *gain){
  int ii;
  float *envelope; 
  float ratio=1.+c.envelope_c/1000.;
  
  switch(c.envelope_mode){
  case 0:
    envelope=rms;
    break;
  default:
    envelope=peak;
    break;
  }
  
  for(ii=0;ii<input_size;ii++){
    float adj=0.;
    float current=envelope[ii];
    float target=current*ratio;
    
    gain[ii]+=target-current;
  }
}

static void suppress(float *peak, float *rms, float *gain,
		     envelope_state *e){
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

  int ii;
  float *envelope; 
  float ratio=c.suppress_ratio/100.;
  float decay=c.suppress_decay/(float)(1024*1024);
  float depth=-c.suppress_depth/10.;
  float chase=e->suppress_decay_chase;
  float undepth=depth/ratio;

  switch(c.suppress_mode){
  case 0:
    envelope=rms;
    break;
  default:
    envelope=peak;
    break;
  }

  for(ii=0;ii<input_size;ii++){
    float current=envelope[ii];

    chase+=decay;
    if(current>chase){
      chase=current;
    }else{
      /* yes, need to expand */
      float difference = chase - current;
      float expanded = difference * ratio;

      if(expanded>depth){
	chase=current+undepth;
	gain[ii]-=depth-undepth;
      }else{
	gain[ii]-=expanded-difference;

      }
    }
  }

  e->suppress_decay_chase=chase;
}

static void final_followers(float *gain,envelope_state *e,int attack,int decay){
  float iirx0,iirx1;
  float iiry0,iiry1;

  float iirg_a;
  float iirg_d;
  float iirc0_a,iirc1_a;
  float iirc0_d,iirc1_d;

  float x,y;
  int k;

  iirg_a=iirg_list[attack];
  iirc0_a=iirc_list[attack][0];
  iirc1_a=iirc_list[attack][1];
  iirg_d=iirg_list[decay];
  iirc0_d=iirc_list[decay][0];
  iirc1_d=iirc_list[decay][1];

  iirx0=e->env1_x[0];
  iirx1=e->env1_x[1];
  iiry0=e->env1_y[0];
  iiry1=e->env1_y[1];

  /* assymetrical filter; in general, fast attack, slow decay */
  for(k=0;k<input_size;k+=2){
    if(iiry0>iiry1){
      /* decay */
      y = (gain[k]+iirx0+iirx0+iirx1)/iirg_d + iiry0*iirc0_d+iiry1*iirc1_d;
      iirx1=gain[k];
      gain[k]=iiry1=y;
    }else{
      /* attack */
      y = (gain[k]+iirx0+iirx0+iirx1)/iirg_a + iiry0*iirc0_a+iiry1*iirc1_a;
      iirx1=gain[k];
      gain[k]=iiry1=y;
    }

    if(iiry0>iiry1){
      /* attack */
      y = (gain[k+1]+iirx1+iirx1+iirx0)/iirg_a + iiry1*iirc0_a+iiry0*iirc1_a;
      iirx0=gain[k+1];
      gain[k+1]=iiry0=y;
    }else{
      /* decay */
      y = (gain[k+1]+iirx1+iirx1+iirx0)/iirg_d + iiry1*iirc0_d+iiry0*iirc1_d;
      iirx0=gain[k+1];
      gain[k+1]=iiry0=y;
    }
  }
  
  e->env1_x[0]=iirx0;
  e->env1_x[1]=iirx1;
  e->env1_y[0]=iiry0;
  e->env1_y[1]=iiry1;

  iirx0=e->env2_x[0];
  iirx1=e->env2_x[1];
  iiry0=e->env2_y[0];
  iiry1=e->env2_y[1];

  for(k=0;k<input_size;k+=2){
    if(iiry0>iiry1){
      /* decay */
      y = (gain[k]+iirx0+iirx0+iirx1)/iirg_d + iiry0*iirc0_d+iiry1*iirc1_d;
      iirx1=gain[k];
      gain[k]=iiry1=y;
    }else{
      /* attack */
      y = (gain[k]+iirx0+iirx0+iirx1)/iirg_a + iiry0*iirc0_a+iiry1*iirc1_a;
      iirx1=gain[k];
      gain[k]=iiry1=y;
    }

    if(iiry0>iiry1){
      /* attack */
      y = (gain[k+1]+iirx1+iirx1+iirx0)/iirg_a + iiry1*iirc0_a+iiry0*iirc1_a;
      iirx0=gain[k+1];
      gain[k+1]=iiry0=y;
    }else{
      /* decay */
      y = (gain[k+1]+iirx1+iirx1+iirx0)/iirg_d + iiry1*iirc0_d+iiry0*iirc1_d;
      iirx0=gain[k+1];
      gain[k+1]=iiry0=y;
    }
  }
  
  e->env2_x[0]=iirx0;
  e->env2_x[1]=iirx1;
  e->env2_y[0]=iiry0;
  e->env2_y[1]=iiry1;

}

static void multicompand_work(float **peakfeed,float **rmsfeed){
  int i,j,k,l;
  int link=c.link_mode;
  float rms[input_size];
  float peak[input_size];
  float gain[input_size];
  int bands=multicomp_freqs[ms.active_bank];

  /* we chase both peak and RMS forward at all times. All portions of
     the compander can use the raw values of one or the other at any
     time */

  for(j=0;j<bands;j++){
    for(i=0;i<input_ch;i++){
      
      //_analysis_append("band",j,ms.ss.lap[j][i],input_size,offset);


      /* RMS chase */
      rms_chase(rms,i,link,ms.rmschoice[ms.active_bank][j],
		ms.e[j],ms.ss.lap[j]);

      /* peak shelving */
      peak_chase(peak,i,link,ms.attack[ms.active_bank][j],
		 ms.ss.lap[j]);

      if(compand_active)
	/* run the static compander */
	static_compand(peak,rms,gain,
		       (bc[ms.active_bank].static_g[j]+c.static_g_trim)/10.,
		       (bc[ms.active_bank].static_e[j]+c.static_e_trim)/10.,
		       (bc[ms.active_bank].static_c[j]+c.static_c_trim)/10.,
		       &ms.e[j][i]);
      else
	memset(gain,0,sizeof(gain));

      /* feedback displays the results of the static compander */
      {
	float *x=ms.ss.lap[j][i];
	float rmsmax=-150,peakmax=-150;
	
	for(k=0;k<input_size;k++){
	  int r=rms[k]+gain[k];
	  x[k]*=fromdB(gain[k]);
	  
	  if(r>rmsmax)rmsmax=r;
	  if(fabs(x[k])>peakmax)peakmax=fabs(x[k]);
	}	    
	
	peakfeed[j][i]=todB(peakmax);
	rmsfeed[j][i]=rmsmax;
      }

      if(compand_active){
	float *x=ms.ss.lap[j][i];

	/* run the range compander */
	range_compand(peak,rms,gain);
	
	/* run the suppressor */
	suppress(peak,rms,gain,ms.e[j]);
	
	//_analysis_append("peak",j,gain,input_size,offset);

	/* Run the Bessel followers */
	final_followers(gain,&ms.e[j][i],
			ms.attack[ms.active_bank][j],
			ms.decay[ms.active_bank][j]);
	
	//_analysis_append("gain",j,gain,input_size,offset);
	

	/* application to data */
	for(k=0;k<input_size;k++)
	  x[k]*=fromdB(gain[k]);

      }else{
	/* else reset the bessel followers */
	ms.e[j][i].env1_x[0]=0.;
	ms.e[j][i].env1_x[1]=0.;
	ms.e[j][i].env1_y[0]=0.;
	ms.e[j][i].env1_y[1]=0.;
	ms.e[j][i].env2_x[0]=0.;
	ms.e[j][i].env2_x[1]=0.;
	ms.e[j][i].env2_y[0]=0.;
	ms.e[j][i].env2_y[1]=0.;
      }
    }
  }

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
    /* reset unused envelope followers to a known state for if/when
       they're brought online */
    
    for(j=(bypass?0:multicomp_freqs[pending]);j<multicomp_freqs_max;j++){
      for(i=0;i<input_ch;i++){
	memset(&ms.e[j][i],0,sizeof(**ms.e));
	ms.e[j][i].static_c_decay_chase=0.;
	ms.e[j][i].static_e_decay_chase=0.;
	ms.e[j][i].static_g_decay_chase=0.;
      }
    }
  }
   
  ms.active_bank=ms.pending_bank;
  bypass_state=bypass;

  return subband_read(in,&ms.ss,&ms.sw[ms.active_bank],
		      multicompand_work,bypass);
}


