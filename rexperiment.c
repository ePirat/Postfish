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

/* arbitrary reconstruction filter.  Postfish uses this for declipping.

   Many thanks to Johnathan Richard Shewchuk and his excellent paper
   'An Introduction to the Conjugate Gradient Method Without the
   Agonizing Pain' for the additional understanding needed to make the
   n^3 -> n^2 log n jump possible. Google for it, you'll find it. */

#include <string.h>
#include "smallft.h"

static void drft_forward_transpose(drft_lookup *fft, double *x){
  int i;
  
  for(i=1;i<fft->n-1;i++)x[i]*=.5;
  drft_backward(fft,x);
}

static void drft_backward_transpose(drft_lookup *fft, double *x){
  int i;
  
  drft_forward(fft,x);
  for(i=1;i<fft->n-1;i++)x[i]*=2.;
}

#include "postfish.h"
#include "test.h"

static void sliding_bark_average(double *f,double *w, int n){
  int lo=0,hi=0,i;
  double acc=0.;

  {
    double bark=toBark(0);
    int newhi=rint(fromBark(bark+.5)*n/44100.)+5;
    acc+=fabs(f[0]);
    for(hi=1;hi<newhi;hi++)acc+=hypot(f[(hi<<1)-1],f[hi<<1]);
    w[0]=acc/hi;
  }

  for(i=1;i<n/2;i++){
    double bark=toBark(44100.*i/n);
    int newlo=rint(fromBark(bark-.5)*n/44100.)-5;
    int newhi=rint(fromBark(bark+.5)*n/44100.)+5;
    if(newhi>n/2)newhi=n/2;

    for(;hi<newhi;hi++)
      acc+=hypot(f[(hi<<1)-1],f[hi<<1]);
    for(;lo<newlo;lo++)
      if(lo==0)
        acc-=fabs(f[0]);
      else
        acc-=hypot(f[(lo<<1)-1],f[lo<<1]);

    w[(i<<1)-1]=w[i<<1]=acc/(hi-lo);
  }
  w[n-1]=w[n-2];
}

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

static double inner_product(double *a, double *b, int n){
  int i;
  double acc=0.;
  for(i=0;i<n;i++)acc+=a[i]*b[i];
  return acc;
}

static void compute_AtAx(drft_lookup *fft,
			 double *x,double *w,int *flag,int mask,int n,
			 double *out){
  int i;

  if(mask){
    for(i=0;i<n;i++)
      if(!flag[i])
	out[i]=0;
      else
	out[i]=x[i];
  }else
    for(i=0;i<n;i++)
      if(flag[i])
	out[i]=0;
      else
	out[i]=x[i];
  
  drft_forward(fft,out);
  for(i=0;i<n;i++)out[i]*=w[i];
  drft_backward(fft,out);

  for(i=0;i<n;i++)
    if(!flag[i])out[i]=0;
  
}

static void compute_Atb_minus_AtAx(drft_lookup *fft,
				   double *x,double *w,double *Atb,int *flag,
				   int n,double *out){
  int i;
  compute_AtAx(fft,x,w,flag,1,n,out);
  for(i=0;i<n;i++)out[i]=Atb[i]-out[i];
}

void reconstruct(drft_lookup *fft,
		 double *x, double *w, int *flag, double e,int max,int n){
  int i,j;
  double Atb[n];
  double r[n];
  double d[n];
  double q[n];
  double phi_new,phi_old,phi_0;
  double alpha,beta;

  /* compute initial Atb */
  compute_AtAx(fft,x,w,flag,0,n,Atb);
  for(j=0;j<n;j++)Atb[j]= -Atb[j];

  compute_Atb_minus_AtAx(fft,x,w,Atb,flag,n,r);
  memcpy(d,r,sizeof(d));
  phi_0=phi_new=inner_product(r,r,n);

  for(i=0;i<max && phi_new>e*e*phi_0;i++){
    compute_AtAx(fft,d,w,flag,1,n,q);
    alpha=phi_new/inner_product(d,q,n);
    for(j=0;j<n;j++)x[j]+=alpha*d[j];

    _analysis("x",i,x,512,0,0);
    {
      double freq[n];
      memcpy(freq,x,sizeof(freq));
      drft_forward(fft,freq);
      _analysis("f",i,freq,512,1,1);
    }

    if((i & 0x3f)==0x3f)
      compute_Atb_minus_AtAx(fft,x,w,Atb,flag,n,r);
    else
      for(j=0;j<n;j++)r[j]-=alpha*q[j];
    
    phi_old=phi_new;
    phi_new=inner_product(r,r,n);
    beta=phi_new/phi_old;
    for(j=0;j<n;j++) d[j]=r[j]+beta*d[j];
  }
}

void precondition(drft_lookup *fft,double *x, 
		  double *w, int *flag, int n,double *out){
  int i;
  memcpy(out,x,sizeof(*x)*n);
  for(i=0;i<n;i++)
    if(!flag[i])out[i]=0;
  drft_backward_transpose(fft,out);
  for(i=0;i<n;i++)out[i]/=w[i]*n;  
  drft_backward(fft,out);
  for(i=0;i<n;i++)out[i]/=n;  
  for(i=0;i<n;i++)
    if(!flag[i])out[i]=0;
}

void reconstruct2(drft_lookup *fft,
		 double *x, double *w, int *flag, double e,int max,int n){
  int i,j;
  double Atb[n];
  double r[n];
  double s[n];
  double d[n];
  double q[n];
  double phi_new,phi_old,phi_0;
  double alpha,beta;

  /* compute initial Atb */
  compute_AtAx(fft,x,w,flag,0,n,Atb);
  for(j=0;j<n;j++)Atb[j]= -Atb[j];

  compute_Atb_minus_AtAx(fft,x,w,Atb,flag,n,r);
  precondition(fft,r,w,flag,n,d);
  phi_0=phi_new=inner_product(r,d,n);

  for(i=0;i<max && phi_new>e*e*phi_0;i++){
    compute_AtAx(fft,d,w,flag,1,n,q);
    alpha=phi_new/inner_product(d,q,n);
    for(j=0;j<n;j++)x[j]+=alpha*d[j];

    _analysis("x1",i,x,512,0,0);
    {
      double freq[n];
      memcpy(freq,x,sizeof(freq));
      drft_forward(fft,freq);
      _analysis("f1",i,freq,512,1,1);
    }

    if((i & 0x3f)==0x3f)
      compute_Atb_minus_AtAx(fft,x,w,Atb,flag,n,r);
    else
      for(j=0;j<n;j++)r[j]-=alpha*q[j];
    
    precondition(fft,r,w,flag,n,s);
    phi_old=phi_new;
    phi_new=inner_product(r,s,n);
    beta=phi_new/phi_old;
    for(j=0;j<n;j++) d[j]=s[j]+beta*d[j];
  }
}

int main(){
  int i,j,k;
  drft_lookup fft;
  int blocksize=512;
  double window[512];
  double work[512];
  double freq[blocksize];
  double w[blocksize];
  int flag[512];

  drft_init(&fft,512);
  for(i=0;i<blocksize;i++) window[i]=sin( (double)i/blocksize*M_PIl);
  for(i=0;i<blocksize;i++) window[i]*=window[i];
  for(i=0;i<blocksize;i++) window[i]=sin(window[i]*M_PIl*.5);

  memcpy(work,testdata[0],sizeof(work));

  for(i=0;i<blocksize;i++){
    flag[i]=0;
    if(work[i]>=.2 || work[i]<=-.2)flag[i]=1;
  }

  for(i=0;i<512;i++)work[i]*=window[i];

  for(i=0;i<512;i++)_analysis("pcm",i,work,512,0,0);
  memcpy(freq,work,sizeof(work));
  drft_forward(&fft,freq);
  for(i=0;i<512;i++)_analysis("freq",i,freq,512,1,1);

  sliding_bark_average(freq,w,blocksize);
  _analysis("w",0,w,512,1,1);

  for(i=0;i<512;i++)w[i]= 1./(w[i]*w[i]);

  reconstruct2(&fft,work,w,flag,0,blocksize,blocksize);

}


