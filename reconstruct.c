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

#include "smallft.h"
#include "reconstruct.h"

/* this setup isn't thread safe, but it's fine for postfish which will
   only access it from a single thread */

drft_lookup fft;
void set_up_filter(int n){
  static int cached_n=0;
  if(n!=cached_n){
    if(n)drft_clear(&fft);
    drft_init(&fft,n);
    cached_n=n;
  }
}

double inner_product(double *a, double *b, int n){
  int i;
  double acc=0.;
  for(i=0;i<n;i++)acc+=a[i]*b[i];
  return acc;
}

void compute_AtAx(double *x,double *w,int *flag,int mask,int n,
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
  
  drft_forward(&fft,out);
  for(i=0;i<n;i++)out[i]*=w[i];
  drft_backward(&fft,out);

  for(i=0;i<n;i++)
    if(!flag[i])out[i]=0;
  
}

void compute_Atb_minus_AtAx(double *x,double *w,double *Atb,int *flag,int n,
			    double *out){
  int i;
  compute_AtAx(x,w,flag,1,n,out);
  for(i=0;i<n;i++)out[i]=Atb[i]-out[i];
}


void reconstruct(double *x, double *w, int *flag, double e,int max,int n){
  int i,j;
  double Atb[n];
  double r[n];
  double d[n];
  double q[n];
  double phi_new,phi_old,phi_0;
  double alpha,beta;

  set_up_filter(n);

  /* compute initial Atb */
  compute_AtAx(x,w,flag,0,n,Atb);
  for(j=0;j<n;j++)Atb[j]= -Atb[j];

  compute_Atb_minus_AtAx(x,w,Atb,flag,n,r);
  memcpy(d,r,sizeof(d));
  phi_0=phi_new=inner_product(r,r,n);

  for(i=0;i<max && phi_new>e*e*phi_0;i++){
    compute_AtAx(d,w,flag,1,n,q);
    alpha=phi_new/inner_product(d,q,n);
    for(j=0;j<n;j++)x[j]+=alpha*d[j];

    if((i & 0x3f)==0x3f)
      compute_Atb_minus_AtAx(x,w,Atb,flag,n,r);
    else
      for(j=0;j<n;j++)r[j]-=alpha*q[j];
    
    phi_old=phi_new;
    phi_new=inner_product(r,r,n);
    beta=phi_new/phi_old;
    for(j=0;j<n;j++) d[j]=r[j]+beta*d[j];
  }
}

