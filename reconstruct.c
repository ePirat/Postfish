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
#include <fftw3.h>
#include <math.h>
#include "reconstruct.h"

/* fftw3 requires this kind of static setup */
static fftwf_plan fftwf_qf;
static fftwf_plan fftwf_qb;
static fftwf_plan fftwf_sf;
static fftwf_plan fftwf_sb;
static float *q;
static float *s;
static int blocksize=0;

void reconstruct_init(int minblock,int maxblock){
  int i;

  q=fftwf_malloc((maxblock+2)*sizeof(*q));
  s=fftwf_malloc((maxblock+2)*sizeof(*s));

  /* fftw priming trick; run it thorugh the paces and prime a plan for
     every size we may need.  fftw will cache the information and not
     need to re-measure later */

  for(i=minblock;i<=maxblock;i<<=1){
    fftwf_qf=fftwf_plan_dft_r2c_1d(i,q,(fftwf_complex *)q,FFTW_MEASURE);
    fftwf_qb=fftwf_plan_dft_c2r_1d(i,(fftwf_complex *)q,q,FFTW_MEASURE);
    fftwf_destroy_plan(fftwf_qf);
    fftwf_destroy_plan(fftwf_qb);
  }
}


void reconstruct_reinit(int n){
  if(blocksize!=n){
    if(blocksize){
      fftwf_destroy_plan(fftwf_qf);
      fftwf_destroy_plan(fftwf_qb);
      fftwf_destroy_plan(fftwf_sf);
      fftwf_destroy_plan(fftwf_sb);
      fftwf_free(q);
      fftwf_free(s);
    }
    blocksize=n;

    q=fftwf_malloc((n+2)*sizeof(*q));
    s=fftwf_malloc((n+2)*sizeof(*s));
    fftwf_qf=fftwf_plan_dft_r2c_1d(n,q,(fftwf_complex *)q,FFTW_MEASURE);
    fftwf_qb=fftwf_plan_dft_c2r_1d(n,(fftwf_complex *)q,q,FFTW_MEASURE);
    fftwf_sf=fftwf_plan_dft_r2c_1d(n,s,(fftwf_complex *)s,FFTW_MEASURE);
    fftwf_sb=fftwf_plan_dft_c2r_1d(n,(fftwf_complex *)s,s,FFTW_MEASURE);
  }
}

static void AtWA(float *w,int n){
  int i;
  fftwf_execute(fftwf_qf);
  for(i=0;i<n+2;i++)q[i]*=w[i];
  fftwf_execute(fftwf_qb); /* this is almost the same as A'; see the
			      correction factor rolled into w at the
			      beginning of reconstruct() */
}

/* This is not the inverse of XA'WAX; the algebra isn't valid (due to
   the singularity of the selection matrix X) and as such is useless
   for direct solution.  However, it does _approximate_ the inverse
   and as such makes an excellent system preconditioner. */
static void precondition(float *w,int n){
  int i;

  /* no need to remove scaling of result; the relative stretching of
     the solution space is what's important */

  fftwf_execute(fftwf_sf); /* almost the same as A^-1'; see the correction
			      factor rolled into w at the beginning of
			      reconstruct() */
  for(i=0;i<n+2;i++)s[i]/=w[i];  
  fftwf_execute(fftwf_sb); 
}

static float inner_product(float *a, float *b, int n){
  int i;
  float acc=0.;
  for(i=0;i<n;i++)acc+=a[i]*b[i];
  return acc;
}

void reconstruct(float *x, float *w,
		 float *flag, float e,int max){
  int n=blocksize;
  int i,j;
  float Atb[n];
  float r[n];
  float d[n];
  float phi_new,phi_old,res_0,res_new;
  float alpha,beta;

  /* hack; roll a correction factor for A'/A-1 into w */
  for(j=1;j<n-1;j++)w[j]*=.5;

  /* compute initial Atb */
  for(j=0;j<n;j++)q[j]=x[j]*(flag[j]-1.);
  AtWA(w,n);
  res_new=res_0=inner_product(q,q,n);
  for(j=0;j<n;j++)Atb[j]=q[j];

  /* compute initial residue */
  for(j=0;j<n;j++)q[j]=x[j]*flag[j];
  AtWA(w,n);
  for(j=0;j<n;j++)s[j]=r[j]=(Atb[j]-q[j])*flag[j];

  /* initial preconditioning */
  precondition(w,n);
  for(j=0;j<n;j++)q[j]=d[j]=s[j]*=flag[j];

  phi_new=inner_product(r,d,n);

  for(i=0;i<max && sqrt(res_new)/sqrt(res_0)>e;i++){

    AtWA(w,n);
    alpha=phi_new/inner_product(d,q,n);
    for(j=0;j<n;j++)x[j]+=alpha*d[j];

    if((i & 0x3f)==0x3f){
      for(j=0;j<n;j++)q[j]=x[j]*flag[j];
      AtWA(w,n);
      for(j=0;j<n;j++)r[j]=(Atb[j]-q[j])*flag[j];
    }else
      for(j=0;j<n;j++)r[j]-=alpha*q[j]*flag[j];
    
    /* apply preconditioner */
    for(j=0;j<n;j++)s[j]=r[j]*flag[j];
    precondition(w,n);
    for(j=0;j<n;j++)s[j]*=flag[j];

    phi_old=phi_new;
    phi_new=inner_product(r,s,n);
    res_new=inner_product(r,r,n);
    beta=phi_new/phi_old;
    for(j=0;j<n;j++) q[j]=d[j]=s[j]+beta*d[j];

  }

}
