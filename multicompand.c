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

#include "multicompand.h"
#include <fftw3.h>
#include "subband.h"

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

static float iirg_list[7]={
  4.875178475e+06,  // 5
  1.008216556e+06,  // 11
  2.524889925e+05,  // 22
  6.3053949794e+04, // 44
  1.5872282930e+04, // 88
  4.086422543e+03,  // 175
  1.049342702e+03   // 350
};

static float iirc_list[7][2]={
  {1.9984308946, -.9984317150},  // 5
  {1.9965490514, -.9965530188},  // 11
  {1.9931020727, -.9931179150},  // 22
  {1.9861887623, -.98625220002}, // 44
  {1.9724411246, -.97269313627}, // 88
  {1.9455669653, -.9465458165},  // 175
  {1.8921217885, -.8959336985}   // 350
};

static float iir_corner_list[7]={5,11,22,44,88,175,350};
static float iir_rmsoffset_list[7]={1048,524,262,131,65,32,16};
static float iir_peakoffset_list[7]={7200,3600,1800,900,450,225,112};

/* ugly, ugly ugly */
static float iirx[2];
static float iiry[2];
static float iirg;
static float iirc[2];

static inline float iir(float x){ 
  float y=
    (iirx[1]+iirx[0]*2.+x)/iirg
    + iiry[1]*iirc[1]
    + iiry[0]*iirc[0];
  iiry[1]=iiry[0];iiry[0]=y;
  iirx[1]=iirx[0];iirx[0]=x;
  return y;
}

typedef struct {
  float peak_shelf_y[1];
  float rms_smooth_y[2];
  float rms_smooth_x[2];

  float main_decay_chase;
  float envelope_decay_chase;
  float suppress_decay_chase;

  float env1[2];
  float env2[2];

} envelope_state;

typedef struct {
  subband_state ss;

  int rmschoice[multicomp_freqs];

  envelope_state **e;
} multicompand_state;

sig_atomic_t compand_visible;
sig_atomic_t compand_active;
multicompand_state ms;
compand_settings c;

void multicompand_reset(){

}

int multicompand_load(void){
  int i,j;
  memset(&ms,0,sizeof(ms));

  subband_load(&ms.ss,multicomp_freq_list,multicomp_freqs);

  /* select the RMS follower filters to use according to 
     sample rate and band frequency */
  for(i=0;i<multicomp_freqs;i++){
    /* select a filter with a corner frequency of about 1/8 the
       band center. Don't go lower than 11 or higher than the 44 */
    double bf=multicomp_freq_list[i];

    ms.rmschoice[i]=3;
    for(j=1;j<4;j++)
      if(iir_corner_list[j]*input_rate/44100 > bf*.125){
	ms.rmschoice[i]=j;
	break;
      }

    fprintf(stderr,"band %d %fHz filter corner %f\n",
	    i,bf,iir_corner_list[ms.rmschoice[i]]);
  }

  ms.e=calloc(input_ch,sizeof(*(ms.e)));
  for(i=0;i<input_ch;i++)
    ms.e[i]=calloc(multicomp_freqs,sizeof(**(ms.e)));

  return 0;
}

static long offset=0;
static void multicompand_work(void){
  float rms[input_size];
  float peak[input_size];
  int i,j,k;

  /* we chase both peak and RMS forward at all times. All portions of
     the compander can use the raw values of one or the other at any
     time */

  for(j=0;j<ms.ss.bands;j++){
    for(i=0;i<input_ch;i++){
      float *x=ms.ss.lap[i][j];
      int ahead=iir_rmsoffset_list[ms.rmschoice[j]];
      /* RMS chase [per channel] */
      iirg=iirg_list[ms.rmschoice[j]];
      iirc[0]=iirc_list[ms.rmschoice[j]][0];
      iirc[1]=iirc_list[ms.rmschoice[j]][1];
      iirx[0]=ms.e[i][j].rms_smooth_x[0];
      iirx[1]=ms.e[i][j].rms_smooth_x[1];
      iiry[0]=ms.e[i][j].rms_smooth_y[0];
      iiry[1]=ms.e[i][j].rms_smooth_y[1];

      for(k=0;k<input_size;k++)
	rms[k]=sqrt(iir(x[k+ahead]*x[k+ahead]));

      _analysis_append("band",j,x,input_size,offset);
      _analysis_append("envelope",j,rms,input_size,offset);

      ms.e[i][j].rms_smooth_x[0]=iirx[0];
      ms.e[i][j].rms_smooth_x[1]=iirx[1];
      ms.e[i][j].rms_smooth_y[0]=iiry[0];
      ms.e[i][j].rms_smooth_y[1]=iiry[1];

    }
  }

  offset+=input_size;
}

time_linkage *multicompand_read(time_linkage *in){
  return subband_read(in,&ms.ss,multicompand_work,
		      !(compand_visible||compand_active));
}
