/*
 *
 *  postfish
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
#define _GNU_SOURCE
#include <stdio.h>
#include "smallft.h"

#include "test.h"

#include <stdlib.h>
#include <math.h>
#define toBark(n)   (13.1f*atan(.00074f*(n))+2.24f*atan((n)*(n)*1.85e-8f)+1e-4f*(n))
#define fromBark(z) (102.f*(z)-2.f*pow(z,2.f)+.4f*pow(z,3.f)+pow(1.46f,z)-1.f)

static void sliding_bark_average(double *f,double *w, int n,double width){
  int i=0,j;
  int lopad=1-rint(fromBark(toBark(0.)-width)*n/44100.);
  int hipad=rint(fromBark(toBark(22050.)+width)*n/44100.)+lopad;
  double acc=0.,del=0.;
  double sec[hipad+1];

  memset(sec,0,sizeof(sec));

  {
    double bark=toBark(0.);
    int hi=rint(fromBark(bark-width)*n/44100.)-1;
    int lo=rint(fromBark(bark+width)*n/44100.)+1;
    double del=fabs(f[0])/(lo-hi);

    double hidel=del/(-hi);
    double lodel=del/(lo);

    sec[hi+lopad]+=hidel;
    sec[lopad]-=hidel;
    sec[lopad]-=lodel;
    sec[lo+lopad]+=lodel;
    
  }

  for(i=1;i<n/2;i++){
    double bark=toBark(44100.*i/n);
    int hi=rint(fromBark(bark-width)*n/44100.)-1;
    int lo=rint(fromBark(bark+width)*n/44100.)+1;
    double del=hypot(f[(i<<1)-1],f[i<<1])/(lo-hi);

    double hidel=del/((i-hi));
    double lodel=del/((lo-i));

    sec[hi+lopad]+=hidel;
    sec[i+lopad]-=hidel;
    sec[i+lopad]-=lodel;
    sec[lo+lopad]+=lodel;

  }

  for(i=0;i<lopad;i++){
    del+=sec[i];
    acc+=del;
  }

  w[0]=acc;
  del+=sec[lopad];
  acc+=del;

  for(i=1;i<n/2;i++){
    w[(i<<1)-1]=w[i<<1]=acc;
    del+=sec[i+lopad];
    acc+=del;

  }
  w[n-1]=w[n-2];
}

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

#define N    128

extern void minimize(drft_lookup *fft,double **A, double *w, double *x, 
		    int *flag, 
		    int piecep, int n1, int n2, int n);


int main(){
  drft_lookup fft;
  
  double **A;
  double x[N*2];
  double s[N*2];
  double f[N*2];
  double w[N*2];
  double wi[N*2];
  int flag[N*2];

  double lap[N];
  int i,j,freex=0,eos=0;
  static int seq=0;
  signed char readbuffer[N*4+44];
  long founddata;

  memset(lap,0,sizeof(lap));
  memset(x,0,sizeof(x));
  /* set up the A matrix needed to compute gradient */
  drft_init(&fft,N*2);
  A=malloc(N*2*sizeof(*A));
  for(i=0;i<N*2;i++)
    A[i]=calloc(N*2,sizeof(**A));
  
  for(i=0;i<N*2;i++){
    A[i][i]=1.;
    drft_forward(&fft,A[i]);
  }

  for(j=0;j<N*2;j++) wi[j]=sin( (double)j/N*M_PIl*.5 );
  for(j=0;j<N*2;j++) wi[j]*=wi[j];
  for(j=0;j<N*2;j++) wi[j]=sin(wi[j]*M_PIl*.5 );

  readbuffer[0] = '\0';
  for (i=0, founddata=0; i<30 && ! feof(stdin) && ! ferror(stdin); i++){
    fread(readbuffer,1,2,stdin);
    fwrite(readbuffer,1,2,stdout);
    
    if ( ! strncmp((char*)readbuffer, "da", 2) ){
      founddata = 1;
      fread(readbuffer,1,6,stdin);
      fwrite(readbuffer,1,6,stdout);
      break;
    }
  }


  while(!eos){
    long bytes=fread(readbuffer,1,N*4,stdin); /* stereo hardwired here */
    if(bytes<N*4)break;

    freex=0;

    /* uninterleave samples */
    for(i=0;i<bytes/4;i++){
      x[i+N]=
        ((readbuffer[i*4+1]<<8)|
         (0x00ff&(int)readbuffer[i*4]))/65536. +
        ((readbuffer[i*4+3]<<8)|
         (0x00ff&(int)readbuffer[i*4+2]))/65536.;
      flag[i+N]=0;
    }

    for(i=0;i<N;i++){
      flag[i+N]=0;
      if(x[i+N]>=.22){
	flag[i+N]=1;
	//x[i+N]=.2;
	freex++;
      }else if(x[i+N]<=-.2){
	flag[i+N]=-1;
	//x[i+N]=-.2;
	freex++;
      }
    }
    
    memcpy(f,x,sizeof(f));
  
    /* weight the bugger */
    for(i=0;i<N*2;i++)f[i]*=wi[i];
    memcpy(s,f,sizeof(s));
    
    if(freex){
      fprintf(stderr,"%d: Free variables: %d\n",seq,freex);

      drft_forward(&fft,f);
      _analysis("pcm",seq,s,N*2,0,0);
      _analysis("f",seq,f,N*2,0,1);
      sliding_bark_average(f,w,N*2,.5);
      //memcpy(w,f,sizeof(w));

      //for(i=1;i<N*2;i+=2)w[i]=w[i+1]=hypot(f[i],f[i+1]);


      _analysis("w",seq,w,N*2,0,1);

      for(i=0;i<N*2;i++)w[i]=1./w[i];

      if(flag[0] && flag[N*2-1]){
	flag[0]=0;
	s[0]=0;
      }
      minimize(&fft,A,w,s,flag,0,0,N*2,N*2);

      memcpy(f,s,sizeof(f));
      _analysis("x",seq,f,N*2,0,0);
      drft_forward(&fft,f);
      _analysis("xf",seq++,f,N*2,0,1);
      
#if 0
      memcpy(f,s,sizeof(s));
      memcpy(s,x,sizeof(s));
      for(i=0;i<N*2;i++)s[i]*=wi[i];
      
      drft_forward(&fft,f);
      sliding_bark_average(f,w,N*2,.5);
      _analysis("w2",seq,w,N*2,0,1);
      for(i=0;i<N*2;i++)w[i]=1./w[i];
      minimize(&fft,A,w,s,flag,0,0,N*2,N*2);
      
      memcpy(f,s,sizeof(f));
      _analysis("x2",seq,f,N*2,0,0);
      drft_forward(&fft,f);
      _analysis("xf2",seq++,f,N*2,0,1);
#endif
    }

    /* lap */
    for(i=0;i<N;i++)lap[i]+=s[i]*wi[i];

    for(i=0;i<bytes/4;i++){
      int val=rint(lap[i]*32768.);
      if(val>32767)val=32767;
      if(val<-32768)val=-32768;
      readbuffer[i*4]=val&0xff;
      readbuffer[i*4+1]=(val>>8)&0xff;
      readbuffer[i*4+2]=val&0xff;
      readbuffer[i*4+3]=(val>>8)&0xff;
    }
    fwrite(readbuffer,1,bytes,stdout);

    for(i=0;i<N;i++)lap[i]=s[i+N]*wi[i+N];
    
    memmove(x,x+N,sizeof(x)-N*sizeof(*x));
    memmove(flag,flag+N,sizeof(flag)-N*sizeof(*flag));
  }
  return 0;
}







