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
#define _GNU_SOURCE

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


/* Autocorrelation LPC coeff generation algorithm invented by
   N. Levinson in 1947, modified by J. Durbin in 1959. */

static double levinson_durbin(double *aut,double *lpc,int m){
  double error=aut[0];
  int i,j;
  
  for(i=0;i<m;i++){
    double r= -aut[i+1];

    if(error==0){
      memset(lpc,0,m*sizeof(*lpc));
      return 0.;
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

    data[i]=work[o]=y;
  }
}

void lpc_extrapolateB(double *lpc,double *prime,int m,
		     double *data,int n){
    long i,j,o,p;
  double y;
  double *work=alloca(sizeof(*work)*(m+n));
  
  if(!prime)
    for(i=0;i<m;i++)
      work[i]=0.;
  else
    for(i=0;i<m;i++)
      work[i]=prime[m-i-1];
  
  for(i=0;i<n;i++){
    y=0;
    o=i;
    p=m;
    for(j=0;j<m;j++)
      y-=work[o++]*lpc[--p];

    data[n-i-1]=work[o]=y;
  }
}




