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
#include "smallft.h"
#include "reconstruct.h"
#include <stdio.h>

extern int input_rate;
extern int input_ch;
extern int input_size;

void _analysis(char *base,int i,double *v,int n,int bark,int dB){
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
      fprintf(of,"%f ",(double)j);
    
    if(dB){
      if(j==0||j==n-1)
        fprintf(of,"%f\n",todB(v[j]));
      else{
        fprintf(of,"%f\n",todB(hypot(v[j],v[j+1])));
        j++;
      }
    }else{
      fprintf(of,"%f\n",v[j]);
    }
  }
  fclose(of);
}

/* accessed only in playback thread/setup */
static drft_lookup fft;
static int blocksize=0;
static int lopad=0,hipad=0;
static u_int32_t *widthlookup=0;
static double *window=0;
static double width=.5;
static double **lap=0;
static double **cache;
static int cache_samples;
static int fillstate=0; /* 0: uninitialized
			   1: normal
			   2: eof processed */

static time_linkage out;

/* accessed across threads */
static sig_atomic_t declip_active=0;
static sig_atomic_t declip_converge=2; /* 0=over, 1=full, 2=half, 3=partial, 4=approx */
static sig_atomic_t *chtrigger=0;
static sig_atomic_t *chactive=0;
static sig_atomic_t pending_blocksize=0;

/* called only by initial setup */
int declip_load(void){
  int i;
  chtrigger=malloc(input_ch*sizeof(*chtrigger));
  chactive=malloc(input_ch*sizeof(*chactive));
  for(i=0;i<input_ch;i++){
    chtrigger[i]=0x80000000UL;
    chactive[i]=1;
  }
  
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

int declip_setactive(int activep,int ch){
  if(ch<0 || ch>=input_ch)return -1;
  chactive[ch]=activep;
  return 0;
}

int declip_settrigger(double trigger,int ch){
  if(ch<0 || ch>=input_ch)return -1;
  chtrigger[ch]=rint(trigger*(1.*0x80000000UL));
  return 0;
}

/* called only in playback thread */
int declip_reset(void){
  /* reset cached pipe state */
  fillstate=0;
  return 0;
}

static void sliding_bark_average(double *f,double *w, int n,double width){
  int i=0,j;
  double acc=0.,del=0.;
  double sec[hipad+1];

  memset(sec,0,sizeof(sec));

  {
    double bark=toBark(0.);
    int hi=widthlookup[0]>>16;
    int lo=widthlookup[0]&(0xffff);
    double del=fabs(f[0])/(lo-hi);

    double hidel=del/(-hi+lopad);
    double lodel=del/(lo-lopad);

    sec[hi]+=hidel;
    sec[lopad]-=hidel;
    sec[lopad]-=lodel;
    sec[lo]+=lodel;
    
  }

  for(i=1;i<n/2;i++){

    double bark=toBark(44100.*i/n);
    int hi=widthlookup[i]>>16;
    int lo=widthlookup[i]&(0xffff);
    double del=hypot(f[(i<<1)-1],f[i<<1])/(lo-hi);

    double hidel=del/((i-hi+lopad));
    double lodel=del/((lo-i-lopad));

    sec[hi]+=hidel;
    sec[i+lopad]-=hidel;
    sec[i+lopad]-=lodel;
    sec[lo]+=lodel;

  }

  for(i=0;i<lopad;i++){
    del+=sec[i];
    acc+=del;
  }

  w[0]=1./(acc*acc);
  del+=sec[lopad];
  acc+=del;

  for(i=1;i<n/2;i++){
    w[(i<<1)-1]=w[i<<1]=1./(acc*acc);
    del+=sec[i+lopad];
    acc+=del;

  }
  w[n-1]=w[n-2];
}

static void declip(double *data,double *lap,double *out,
		   int blocksize,unsigned long trigger){
  double freq[blocksize];
  int    flag[blocksize];
  double triggerlevel=trigger*(1./0x80000000UL);
  double epsilon=1e-12;
  int    iterbound=blocksize,i,j,count=0;
  
  for(i=0;i<blocksize/8;i++)flag[i]=0;
  for(;i<blocksize*7/8;i++){
    flag[i]=0;
    if(data[i]>=trigger || data[i]<=-trigger){
      flag[i]=1;
      count++;
    }
  }
  for(;i<blocksize;i++)flag[i]=0;
  for(i=0;i<blocksize;i++)data[i]*=window[i];
  memcpy(freq,data,sizeof(freq));
  drft_forward(&fft,freq);
  sliding_bark_average(freq,freq,blocksize,width);
  
  switch(declip_converge){
  case 0:
    epsilon=1e-12;
    iterbound=blocksize*2;
    break;
  case 1:
    epsilon=1e-8;
    iterbound=count;
    break;
  case 2:
    epsilon=1e-6;
    iterbound=count/2;
    break;
  case 3:
    epsilon=1e-5;
    iterbound=count/4;
    break;
  case 4:
    epsilon=1e-3;
    iterbound=count/8;
    break;
  }
  if(iterbound<20)iterbound=20;

  reconstruct(&fft,data,freq,flag,epsilon*count,iterbound,blocksize);

  if(out)
    for(i=0;i<blocksize/2;i++)
      out[i]=lap[i]+data[i]*window[i];

  for(i=blocksize/2,j=0;i<blocksize;i++)
    lap[j]=data[i]*window[i];
}

/* called only by playback thread */
time_linkage *declip_read(time_linkage *in){
  int i;
  double work[blocksize];

  if(pending_blocksize!=blocksize){
    if(blocksize){
      free(widthlookup);
      free(window);
      drft_clear(&fft);
    }
    blocksize=pending_blocksize;

    widthlookup=malloc((blocksize>>1)*sizeof(*widthlookup));
    for(i=0;i<blocksize/2;i++){
      double bark=toBark(input_rate*i/blocksize);
      int hi=rint(fromBark(bark-width)*blocksize/input_rate)-1+lopad;
      int lo=rint(fromBark(bark+width)*blocksize/input_rate)+1+lopad;
      
      if(hi<0 || lo<0 || hi>65535 || lo<65535) return 0;
      widthlookup[i]=(hi<<16)+lo;
    }
    lopad=1-rint(fromBark(toBark(0.)-width)*blocksize/input_rate);
    hipad=rint(fromBark(toBark(input_rate*.5)+width)*blocksize/input_rate)+lopad;
    
    drft_init(&fft,blocksize);

    window=malloc(blocksize*sizeof(*window));
    for(i=0;i<blocksize/8;i++) window[i]=0.;
    for(;i<blocksize*3/8;i++) window[i]=sin( (double)(i-blocksize/8)/blocksize*M_PIl );
    for(;i<blocksize*5/8;i++) window[i]=1.;
    for(;i<blocksize*7/8;i++) window[i]=sin( (double)(blocksize*7/8-i)/blocksize*M_PIl );
    for(;i<blocksize;i++) window[i]=0.;    
    for(i=0;i<blocksize;i++) window[i]*=window[i];
    for(i=0;i<blocksize;i++) window[i]=sin(window[i]*M_PIl);

  }

  switch(fillstate){
  case 0: /* prime the lapping and cache */
    for(i=0;i<input_ch;i++){
      int j;
      double *temp=in->data[i];
      if(chactive[i] && declip_active){
	memset(work,0,sizeof(*work)*blocksize/2);
	memcpy(work+blocksize/2,temp,sizeof(*work)*blocksize/2);
	declip(work,lap[i],0,blocksize,chtrigger[i]);
      }else{
	for(j=0;j<blocksize/2;j++)
	  lap[i][j]=window[j+blocksize/2]*temp[j];
      }
      memset(cache[i],0,sizeof(**cache)*input_size);
      in->data[i]=cache[i];
      cache[i]=temp;
    }
    cache_samples=in->samples;
    fillstate=1;
    if(in->samples==in->size)return 0;
    in->samples=0;
    /* fall through */
  case 1: /* nominal processing */
    for(i=0;i<input_ch;i++){
      double *temp=cache[i];
	int j;
      if(chactive[i] && declip_active){
	for(j=0;j+blocksize<out.size;j+=blocksize/2){
	  memcpy(work,temp+j,sizeof(*work)*blocksize);
	  declip(work,lap[i],out.data[i]+j,blocksize,chtrigger[i]);
	}
	memcpy(work,temp+j,sizeof(*work)*blocksize/2);
	memcpy(work+blocksize/2,in->data[i],sizeof(*work)*blocksize/2);
	declip(work,lap[i],out.data[i]+j,blocksize,chtrigger[i]);
      }else{
	memcpy(out.data[i],temp,out.size*sizeof(*temp));
	for(j=0;j<blocksize/2;j++)
	  lap[i][j]=window[j+blocksize/2]*temp[j];
      }
      
      in->data[i]=cache[i];
      cache[i]=temp;
    }
    out.samples=cache_samples;
    cache_samples=in->samples;
    if(out.samples<out.size)fillstate=2;
    break;
  case 2: /* we've pushed out EOF already */
    return 0;
  }
  return &out;
}
