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
#include "reconstruct.h"

static void AtWA(drft_lookup *fft, double *x, double *w,int n){
  int i;
  drft_forward(fft,x);
  for(i=0;i<n;i++)x[i]*=w[i];
  drft_backward(fft,x); /* this is almost the same as A'; see the
			   correction factor rolled into w at the
			   beginning of reconstruct() */
}

/* This is not the inverse of XA'WAX; the algebra isn't valid (due to
   the singularity of the selection matrix X) and as such is useless
   for direct solution.  However, it does _approximate_ the inverse
   and as such makes an excellent system preconditioner. */
static void precondition(drft_lookup *fft, double *x, double *w,int n){
  int i;

  /* no need to remove scaling of result; the relative stretching of
     the solution space is what's important */

  drft_forward(fft,x); /* almost the same as A^-1'; see the correction
			  factor rolled into w at the beginning of
			  reconstruct() */
  for(i=0;i<n;i++)x[i]/=w[i];  
  drft_backward(fft,x);
}

static double inner_product(double *a, double *b, int n){
  int i;
  double acc=0.;
  for(i=0;i<n;i++)acc+=a[i]*b[i];
  return acc;
}

#include <stdio.h>
void reconstruct(drft_lookup *fft,
		 double *x, double *w, 
		 double *flag, double e,int max,int n){
  int i,j;
  double Atb[n];
  double r[n];
  double d[n];
  double q[n];
  double s[n];
  double phi_new,phi_old,res_0,res_new;
  double alpha,beta;

  /* hack; roll a correction factor for A'/A-1 into w */
  for(j=1;j<n-1;j++)w[j]*=.5;

  /* compute initial Atb */
  for(j=0;j<n;j++)Atb[j]=x[j]*(flag[j]-1.);
  AtWA(fft,Atb,w,n);

  /* compute initial residue */
  for(j=0;j<n;j++)r[j]=x[j]*flag[j];
  AtWA(fft,r,w,n);
  for(j=0;j<n;j++)d[j]=r[j]=(Atb[j]-r[j])*flag[j];

  /* initial preconditioning */
  precondition(fft,d,w,n);
  for(j=0;j<n;j++)q[j]=d[j]*=flag[j];

  phi_new=inner_product(r,d,n);
  res_new=res_0=inner_product(Atb,Atb,n);

  for(i=0;i<max && sqrt(res_new)/sqrt(res_0)>e;i++){
    AtWA(fft,q,w,n);
    alpha=phi_new/inner_product(d,q,n);
    for(j=0;j<n;j++)x[j]+=alpha*d[j];

    if((i & 0x3f)==0x3f){
      for(j=0;j<n;j++)r[j]=x[j]*flag[j];
      AtWA(fft,r,w,n);
      for(j=0;j<n;j++)r[j]=(Atb[j]-r[j])*flag[j];
    }else
      for(j=0;j<n;j++)r[j]-=alpha*q[j]*flag[j];
    
    /* apply preconditioner */
    for(j=0;j<n;j++)s[j]=r[j]*flag[j];
    precondition(fft,s,w,n);
    for(j=0;j<n;j++)s[j]*=flag[j];

    phi_old=phi_new;
    phi_new=inner_product(r,s,n);
    res_new=inner_product(r,r,n);
    beta=phi_new/phi_old;
    for(j=0;j<n;j++) q[j]=d[j]=s[j]+beta*d[j];

  }

}

