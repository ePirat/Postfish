/*
 *
 *  postfish.c
 *    
 *      Copyright (C) 2002-2005 Monty
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

/* The LPC filters are used to extrapolate past the initial and final
   PCM 'cliffs' to remove most of the frequency-domain pollution from the
   abrupt edges */

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

#include "postfish.h"
#include "lpc.h"

/* this version is from the Vorbis source code */
static float lpc_from_data(float *data,float *coeff,int n,int m){
  double *aut=alloca(sizeof(*aut)*(m+1));
  double *lpc=alloca(sizeof(*lpc)*(m));
  double error;
  int i,j;

  /* autocorrelation, p+1 lag coefficients */
  j=m+1;
  while(j--){
    double d=0; /* double needed for accumulator depth */
    for(i=j;i<n;i++)d+=(double)data[i]*data[i-j];
    aut[j]=d;
  }
  
  /* Generate lpc coefficients from autocorr values */

  error=aut[0];
  
  for(i=0;i<m;i++){
    double r= -aut[i+1];

    if(error==0){
      memset(coeff,0,m*sizeof(*coeff));
      return 0;
    }

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

  for(j=0;j<m;j++)coeff[j]=(float)lpc[j];

  /* we need the error value to know how big an impulse to hit the
     filter with later */
  
  return error;
}

static void lpc_predict(float *coeff,float *data,int m,long n){
    
  long i,j,o,p;
  float y;
  for(i=0;i<n;i++){
    y=0;o=i;p=m;
    for(j=0;j<m;j++) y-=data[o++]*coeff[--p];
    data[o]=y;
  }
}

void preextrapolate_helper(float *data,int n,float *blank,int size){
  int j;
  int order=32;
  float *lpc=alloca(order*sizeof(*lpc));
  float *work=alloca(sizeof(*work)*(n+size));

  /* reverse data */
  for(j=0;j<n;j++)
    work[j]=data[n-j-1];
  
  /* construct filter */
  lpc_from_data(work,lpc,n,order);
  
  /* run filter forward */
  lpc_predict(lpc,work+n-order,order,size);
  
  /* re-reverse data */
  for(j=0;j<size;j++)
    blank[size-j-1]=work[n+j];

}

void postextrapolate_helper(float *cache,int n1,float *data,int n2,
			    float *blank, int size){
  int j;
  int order=32;
  float *lpc=alloca(order*sizeof(*lpc));
  float *work=alloca(sizeof(*work)*(n1+n2+size));
  
  for(j=0;j<n1;j++)
    work[j]=cache[j];
  for(j=0;j<n2;j++)
    work[j+n1]=data[j];
  
  /* construct filter */
  lpc_from_data(work,lpc,n1+n2,order);
  
  /* run filter forward */
  lpc_predict(lpc,work+n1+n2-order,order,size);
  
  for(j=0;j<size;j++)
    blank[j]=work[n1+n2+j];

}

