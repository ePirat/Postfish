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

/* Implment a linear system least-squares minimizer.  Typical
   implementations are inefficient due to our need for a weighting
   vector that changes from frame to frame, eliminating our ability to
   presolve the first- and second-order differential matricies.  This
   alternate to Newton's Method allows fast worst-case O(N^2) direct
   solution of the least-squares fit. */

/* if no 'x' vector, assume zeroes.  If no slope vector, assume unit slope */
static double line_minimize(double **A, double *x,double *slope,
			    double *c,double *w,
			    int *flag,int n, double *grad){
  double c2[n];
  double A2[n];
  double f1=0.,f2=0.,delta;
  int i,j;

  for(i=0;i<n;i++){
    c2[i]=c[i];
    A2[i]=0;
    if(x && flag[i]){
      double *Ai=A[i];
      if(slope){
	for(j=0;j<n;j++)
	  if(flag[j]){
	    A2[i]+=Ai[j]*s[j];
	    c2[i]+=Ai[j]*x[j];
	  }
      }else
	for(j=0;j<n;j++)
	  if(flag[j]){
	    A2[i]+=Ai[j];
	    c2[i]+=Ai[j]*x[j];
	  }
    }
  }
  
  /* calculate first derivative */
  for(i=0;i<n;i++)if(flag[i]) f1+= 2. * A2[i] * c2[i] * w[i];

  /* calculate second derivative */
  for(i=0;i<n;i++)if(flag[i]) f2+= 2. * A2[i] * A2[i] * w[i];

  /* thus our minimum is at... */
  delta = -f1/f2;

  /* do we need a new grad? */
  if(grad){
    for(i=0;i<n;i++)c2[i]+=A2[i]*delta;
    for(j=0;j<n;j++){
      grad[j]=0.;
      if(flag[j])
	for(i=0;i<n;i++)
	  grad[j]+= 2. * c2[i] * A[i][j] * w[i];
    }
  }

  return delta;
}

double Nspace_minimum(double **A, double *x, double *w, int *flag,
		      double *outgrad,int n){
  int i,j;
  double constants[n];
  double delta;
  double delta2;
  double work[n];
  double slope[n];
  double grad[n];

  /* everything we're concerned with is in quadratic form, so Newton's
     method will solve minimization directly.  We don't do the
     minimization in n-space as computation of the second derivative
     of our full cost function is N^3 (due to the weighting vector).
     However, we can perform the same operation with three 1-d
     minimizations in n-space */

  /* begin by assuming the only free variables we have are the x's
     marked by flag.  The fixed x's immediately collapse out of the
     original cost function and into constants. */
  for(i=0;i<n;i++){
    constants[i]=0;
    if(flag[i]){
      double *Ai=A[i];
      for(j=0;j<n;j++)
	if(!flag[j])constants[i]+=Ai[j]*x[j];
    }
  }

  /* initially use an arbitrary slope to define a line passing
     through the origin of the n-space defined by the free variables;
     a unit slope is convenient. */
  
  /* Fix the free variables to zero; rewrite the cost function as a
     function of a single variable 'delta' that scales the unit slope.
     Minimize the rewritten cost function for delta. Also get the grad
     at the minimum as the new slope (which falls out at low cost as a
     normal vector, allowing easy construction of a parallel line for
     the second step) */
  delta=line_minimize(A,NULL,NULL,constants,w,flag,n,grad);

  /* offset the origin by the gradient, and set up a new line
     minimization */
  delta2=delta-line_minimize(A,grad,NULL,constants,w,flag,n,NULL);
   
  /* construct a line passing through the two minima */
  /* faster not to if()-out the unused elements */
  for(i=0;i<n;i++){
    work[i]=delta;
    slope[i]=grad[i]+delta2;
  }

  /* one last minimization.  We might need the gradient */
  delta2=line_minimize(A,work,slope,constants,w,flag,n,outgrad);

  /* compute new x's */
  for(i=0;i<n;i++)
    if(flag[i])
      x[i]=work[i]+slope[i]*delta2;
}
