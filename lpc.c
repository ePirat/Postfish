/*
 *
 *  postfish.c
 *    
 *      Copyright (C) 2002-2003 Monty
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

/* lpc_from_data is derived from code written by Jutta Degener and
   Carsten Bormann; thus we include their copyright below.  */

/* Preserved Copyright: *********************************************/

/* Copyright 1992, 1993, 1994 by Jutta Degener and Carsten Bormann,
Technische Universita"t Berlin

Any use of this software is permitted provided that this notice is not
removed and that neither the authors nor the Technische Universita"t
Berlin are deemed to have made any representations as to the
suitability of this software for any purpose nor are held responsible
for any defects of this software. THERE IS ABSOLUTELY NO WARRANTY FOR
THIS SOFTWARE.

As a matter of courtesy, the authors request to be informed about uses
this software has found, about bugs in this software, and about any
improvements that may be of general interest.

Berlin, 28.11.1994
Jutta Degener
Carsten Bormann

*********************************************************************/

#define _ISOC99_SOURCE

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "lpc.h"
#include "smallft.h"

static void autocorr(double *data,double *aut,int n,int m){
  int i,j;
  
  /* autocorrelation, p+1 lag coefficients */
  j=m+1;
  while(j--){
    double d=0.; /* double needed for accumulator depth */
    for(i=j;i<n;i++)d+=(double)data[i]*data[i-j];
    aut[j]=d;
  }
}

static void autocorr_incomplete(double *data,double *aut,int n,int m){
  int i,j;
  
  /* autocorrelation, p+1 lag coefficients */
  j=m+1;
  while(j--){
    int count=0;
    double d=0.; /* double needed for accumulator depth */
    for(i=j;i<n;i++){
      if(!isnan(data[i]) && !isnan(data[i-j])){
	d+=data[i]*data[i-j];
	count++;
      }
    }
    aut[j]=d;
  }
}

/* Autocorrelation LPC coeff generation algorithm invented by
   N. Levinson in 1947, modified by J. Durbin in 1959. */

static double levinson_durbin(double *aut,double *lpc,int m){
  double error=aut[0];
  int i,j;
  
  if(error==0){
    memset(lpc,0,m*sizeof(*lpc));
    return 0.;
  }

  for(i=0;i<m;i++){
    double r= -aut[i+1];

    /* Sum up this iteration's reflection coefficient; note that in
       Vorbis we don't save it.  If anyone wants to recycle this code
       and needs reflection coefficients, save the results of 'r' from
       each iteration. */

    for(j=0;j<i;j++)r-=lpc[j]*aut[i-j];
    r/=error; 

    /* Update LPC coefficients and total error */
    
    lpc[i]=r;
    for(j=0;j<i/2;j++){
      double tmp=lpc[j];

      lpc[j]+=r*lpc[i-1-j];
      lpc[i-1-j]+=r*tmp;
    }
    if(i%2)lpc[j]+=lpc[j]*r;

    error*=1.f-r*r;
  }

  return error;
}

/* input: data: training vector
             n: size of training vector
	     m: order of filter
	     lpc: vector of size m */
          
double lpc_from_data(double *data,double *lpc,int n,int m){
  double *aut=alloca(sizeof(*aut)*(m+1));
  autocorr(data,aut,n,m);
  return levinson_durbin(aut,lpc,m);
}

double lpc_from_incomplete_data(double *data,double *lpc,int n,int m){
  double *aut=alloca(sizeof(*aut)*(m+1));
  autocorr_incomplete(data,aut,n,m);
  return levinson_durbin(aut,lpc,m);
}

void lpc_extrapolate(double *lpc,double *prime,int m,
		     double *data,int n){
  
  /* in: coeff[0...m-1] LPC coefficients 
     prime[0...m-1] initial values (allocated size of n+m-1)
     out: data[0...n-1] data samples */
  
  long i,j,o,p;
  double y;
  double *work=alloca(sizeof(*work)*(m+n));
  
  if(!prime)
    for(i=0;i<m;i++)
      work[i]=0.;
  else
    for(i=0;i<m;i++)
      work[i]=prime[i];
  
  for(i=0;i<n;i++){
    y=0;
    o=i;
    p=m;
    for(j=0;j<m;j++)
      y-=work[o++]*lpc[--p];
    
    data[i]=work[o]=y+data[i];
  }
}

void lpc_subtract(double *lpc,double *prime,int m,
		  double *data,int n){
  
  long i,j,o,p;
  double y;
  double *work=alloca(sizeof(*work)*(m+n));
  
  if(!prime)
    for(i=0;i<m;i++)
      work[i]=0.;
  else
    for(i=0;i<m;i++)
      work[i]=prime[i];
  
  for(i=0;i<n;i++){
    y=0;
    o=i;
    p=m;
    for(j=0;j<m;j++)
      y-=work[o++]*lpc[--p];
    work[o]=data[i];
    data[i]-=y;
  }
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define todB(x)   ((x)==0?-140.f:log((x)*(x))*4.34294480f)
#define toBARK(n)   (13.1f*atan(.00074f*(n))+2.24f*atan((n)*(n)*1.85e-8f)+1e-4f*(n))

void _analysis(char *base,int i,double *v,int n,int bark,int dB){
  int j;
  FILE *of;
  char buffer[80];

  sprintf(buffer,"%s_%d.m",base,i);
  of=fopen(buffer,"w");
  
  if(!of)perror("failed to open data dump file");
  
  for(j=0;j<n;j++){
    if(bark){
      float b=toBARK((4000.f*j/n)+.25);
      fprintf(of,"%f ",b);
    }else
      fprintf(of,"%f ",(double)j);
    
    if(dB){
      fprintf(of,"%f\n",todB(v[j]));
    }else{
      fprintf(of,"%f\n",v[j]);
    }
  }
  fclose(of);
}

void lpc_to_curve(double *curve,double *lpc,int n,int m,double amp){
  int i;
  drft_lookup fft;
  double *work=alloca(sizeof(*work)*n*2);
  memset(work,0,sizeof(double)*n*2);

  for(i=0;i<m;i++){
    work[i*2+1]=lpc[i]/(4*amp);
    work[i*2+2]=-lpc[i]/(4*amp);
  }
  
  drft_init(&fft,n*2);
  drft_backward(&fft,work); /* reappropriated ;-) */
  drft_clear(&fft);

  {
    int n2=n*2;
    double unit=1./amp;
    curve[0]=(1./(work[0]*2+unit));
    for(i=1;i<n;i++){
      double real=(work[i]+work[n2-i]);
      double imag=(work[i]-work[n2-i]);
      double a = real + unit;
      curve[i] = 1.0 / hypot(a, imag);
    }
  }
}  

#define N    1024
#define M    32
#define READ (N+M)

signed char readbuffer[READ*4+44];

main(){
  static int seq;
  int eos=0,ret;
  int i, founddata;

  readbuffer[0] = '\0';
  for (i=0, founddata=0; i<30 && ! feof(stdin) && ! ferror(stdin); i++)
  {
    fread(readbuffer,1,2,stdin);

    if ( ! strncmp((char*)readbuffer, "da", 2) )
    {
      founddata = 1;
      fread(readbuffer,1,6,stdin);
      break;
    }
  }

  while(!eos){
    double      samples[READ];
    double      curve[N];
    double      lpc[M];

    long i;
    long bytes=fread(readbuffer,1,READ*4,stdin); /* stereo hardwired here */
    
    if(bytes<READ*4)break;
      
    /* uninterleave samples */
    for(i=0;i<bytes/4;i++)
      samples[i]=
	((readbuffer[i*4+1]<<8)|
	 (0x00ff&(int)readbuffer[i*4]))/32768. +
	((readbuffer[i*4+3]<<8)|
	 (0x00ff&(int)readbuffer[i*4+2]))/32768.f;
    
    
    _analysis("pcm",seq,samples,READ,0,0);
    lpc_from_data(samples,lpc,READ,M);
    _analysis("lpc",seq,lpc,M,0,0);
    lpc_to_curve(curve,lpc,N,M,1.0);
    _analysis("H",seq,curve,N,1,1);


    /* kill off samples */
    {
      double max=0;
      for(i=0;i<READ;i++)
	if(samples[i]>max)max=samples[i];
      for(i=0;i<READ;i++){
	if(samples[i]>max*.50)samples[i]=max*.50;
	if(samples[i]<-max*.50)samples[i]=-max*.50;
      }
    }
    _analysis("pcmi",seq,samples,READ,0,0);
    lpc_from_incomplete_data(samples,lpc,READ,M);
    _analysis("lpci",seq,lpc,M,0,0);
    lpc_to_curve(curve,lpc,N,M,1.0);
    _analysis("Hi",seq,curve,N,1,1);
    lpc_subtract(lpc,samples,M,samples+M,N);
    _analysis("resi",seq,samples,READ,0,0);



    seq++;
  }
  

  return 0;
}

