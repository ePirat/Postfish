/*
 *
 *  postfish
 *    
 *      Copyright (C) 2002-2004 Monty
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

/* code derived directly from mkfilter by the late A.J. Fisher,
   University of York <fisher@minster.york.ac.uk> September 1992; this
   is only the minimum code needed to build an arbitrary Bessel filter */

#include "postfish.h"
#include "bessel.h"

typedef struct { 
  double re, im;
} complex;

static complex bessel_poles[] = {
  { -1.00000000000e+00, 0.00000000000e+00}, 
  { -1.10160133059e+00, 6.36009824757e-01},
  { -1.32267579991e+00, 0.00000000000e+00}, 
  { -1.04740916101e+00, 9.99264436281e-01},
  { -1.37006783055e+00, 4.10249717494e-01}, 
  { -9.95208764350e-01, 1.25710573945e+00},
  { -1.50231627145e+00, 0.00000000000e+00}, 
  { -1.38087732586e+00, 7.17909587627e-01},
  { -9.57676548563e-01, 1.47112432073e+00}, 
  { -1.57149040362e+00, 3.20896374221e-01},
  { -1.38185809760e+00, 9.71471890712e-01}, 
  { -9.30656522947e-01, 1.66186326894e+00},
  { -1.68436817927e+00, 0.00000000000e+00}, 
  { -1.61203876622e+00, 5.89244506931e-01},
  { -1.37890321680e+00, 1.19156677780e+00}, 
  { -9.09867780623e-01, 1.83645135304e+00},
  { -1.75740840040e+00, 2.72867575103e-01}, 
  { -1.63693941813e+00, 8.22795625139e-01},
  { -1.37384121764e+00, 1.38835657588e+00}, 
  { -8.92869718847e-01, 1.99832584364e+00},
  { -1.85660050123e+00, 0.00000000000e+00}, 
  { -1.80717053496e+00, 5.12383730575e-01},
  { -1.65239648458e+00, 1.03138956698e+00}, 
  { -1.36758830979e+00, 1.56773371224e+00},
  { -8.78399276161e-01, 2.14980052431e+00},
  { -1.92761969145e+00, 2.41623471082e-01},
  { -1.84219624443e+00, 7.27257597722e-01}, 
  { -1.66181024140e+00, 1.22110021857e+00},
  { -1.36069227838e+00, 1.73350574267e+00}, 
  { -8.65756901707e-01, 2.29260483098e+00},
};

#define TWOPI	    (2.0 * M_PIl)
#define EPS	    1e-10
#define MAXPZ	    8

typedef struct { 
  complex poles[MAXPZ], zeros[MAXPZ];
  int numpoles, numzeros;
} pzrep;

static complex cdiv(complex z1, complex z2){ 
  double mag = (z2.re * z2.re) + (z2.im * z2.im);
  complex ret={((z1.re * z2.re) + (z1.im * z2.im)) / mag,
	       ((z1.im * z2.re) - (z1.re * z2.im)) / mag};
  return ret;
}

static complex cmul(complex z1, complex z2){ 
  complex ret={z1.re*z2.re - z1.im*z2.im,
	       z1.re*z2.im + z1.im*z2.re};
  return ret;
}
static complex cadd(complex z1, complex z2){ 
  z1.re+=z2.re;
  z1.im+=z2.im;
  return z1;
}

static complex csub(complex z1, complex z2){ 
  z1.re-=z2.re;
  z1.im-=z2.im;
  return z1;
}

static complex cconj(complex z){ 
  z.im = -z.im;
  return z;
}

static complex eval(complex coeffs[], int npz, complex z){ 
  complex sum = (complex){0.0,0.0};
  int i;
  for (i = npz; i >= 0; i--) sum = cadd(cmul(sum, z), coeffs[i]);
  return sum;
}

static complex evaluate(complex topco[], int nz, complex botco[], int np, complex z){ 
  return cdiv(eval(topco, nz, z),eval(botco, np, z));
}

static complex blt(complex pz){ 
  complex two={2.,0.};
  return cdiv(cadd(two, pz), csub(two, pz));
}

static void multin(complex w, int npz, complex coeffs[]){ 
  int i;
  complex nw = (complex){-w.re , -w.im};
  for (i = npz; i >= 1; i--) 
    coeffs[i] = cadd(cmul(nw, coeffs[i]), coeffs[i-1]);
  coeffs[0] = cmul(nw, coeffs[0]);
}
 
static void expand(complex pz[], int npz, complex coeffs[]){ 
  /* compute product of poles or zeros as a polynomial of z */
  int i;
  coeffs[0] = (complex){1.0,0.0};
  for (i=0; i < npz; i++) coeffs[i+1] = (complex){0.0,0.0};
  for (i=0; i < npz; i++) multin(pz[i], npz, coeffs);
  /* check computed coeffs of z^k are all real */
  for (i=0; i < npz+1; i++){ 
    if (fabs(coeffs[i].im) > EPS){ 
      fprintf(stderr, "mkfilter: coeff of z^%d is not real; poles/zeros are not complex conjugates\n", i);
      exit(1);
    }
  }
}

double mkbessel(double raw_alpha,int order,double *ycoeff){ 
  int i,p= (order*order)/4; 
  pzrep splane, zplane;
  complex topcoeffs[MAXPZ+1], botcoeffs[MAXPZ+1];
  double warped_alpha;
  complex dc_gain;

  memset(&splane,0,sizeof(splane));
  memset(&zplane,0,sizeof(zplane));

  if (order & 1) splane.poles[splane.numpoles++] = bessel_poles[p++];
  for (i = 0; i < order/2; i++){ 
    splane.poles[splane.numpoles++] = bessel_poles[p];
    splane.poles[splane.numpoles++] = cconj(bessel_poles[p++]);
  }
  
  warped_alpha = tan(M_PIl * raw_alpha) / M_PIl;
  for (i = 0; i < splane.numpoles; i++){
    splane.poles[i].re *= TWOPI * warped_alpha;
    splane.poles[i].im *= TWOPI * warped_alpha;
  }
  
  zplane.numpoles = splane.numpoles;
  zplane.numzeros = splane.numzeros;
  for (i=0; i < zplane.numpoles; i++) 
    zplane.poles[i] = blt(splane.poles[i]);
  for (i=0; i < zplane.numzeros; i++) 
    zplane.zeros[i] = blt(splane.zeros[i]);
  while (zplane.numzeros < zplane.numpoles) 
    zplane.zeros[zplane.numzeros++] = (complex){-1.0,0.};
  
  expand(zplane.zeros, zplane.numzeros, topcoeffs);
  expand(zplane.poles, zplane.numpoles, botcoeffs);
  dc_gain = evaluate(topcoeffs, zplane.numzeros, botcoeffs, zplane.numpoles, (complex){1.0,0.0});

  for(i=0;i<order;i++)
    ycoeff[order-i-1] = -(botcoeffs[i].re / botcoeffs[zplane.numpoles].re);

  return hypot(dc_gain.re,dc_gain.im);
}

/* assymetrical attack/decay filter computation */
/* this one is designed for fast attack, slow decay */
void compute_iir_fast_attack2(float *x, int n, iir_state *is, 
			     iir_filter *attack, iir_filter *decay){
  double a_c0=attack->c[0],d_c0=decay->c[0];
  double a_c1=attack->c[1],d_c1=decay->c[1];
  double a_g=attack->g, d_g=decay->g;
  
  double x0=is->x[0],x1=is->x[1];
  double y0=is->y[0],y1=is->y[1];
  int state=is->state;
  int i=0;

  if(x[0]>y0)state=0; 
      
  while(i<n){
    
    if(state==0){
      /* attack case */
      while(i<n){
	double ya= (x[i]+x0*2.+x1)/a_g + y0*a_c0+y1*a_c1;
    
	if(ya<y0){
	  state=1; 
	  break;
	}
	x1=x0;x0=x[i];
	y1=y0;x[i]=y0=ya;
	i++;
      }
    }

    if(state==1){
      /* decay case */
      if(y1<y0){
	/* decay fixup needed because we're in discontinuous time */
	  y1=y0;
      }

      while(1){
	double yd = (x[i]+x0*2.+x1)/d_g + y0*d_c0+y1*d_c1;

	x1=x0;x0=x[i];
	y1=y0;x[i]=y0=yd;
	i++;

	if(i>=n)break;
	if(x[i]>y0){
	  state=0;
	  break;
	}
      }
    }
  }
  
  is->x[0]=x0;is->x[1]=x1;
  is->y[0]=y0;is->y[1]=y1;
  is->state=state;
  
}

/* allow decay to proceed in freefall */
void compute_iir_freefall1(float *x, int n, iir_state *is, 
			   iir_filter *decay){
  double d_c0=decay->c[0];
  
  double x0=is->x[0];
  double y0=is->y[0];
  int i=0;
  
  while(i<n){
    double yd;

     yd = y0*d_c0;

    if(x[i]>yd)yd=x[i];

    x0=x[i];
    x[i]=y0=yd;
    i++;
  }
  
  is->x[0]=x0;
  is->y[0]=y0;
  
}

void compute_iir_freefall2(float *x, int n, iir_state *is, 
			  iir_filter *decay){
  double d_c0=decay->c[0];
  double d_c1=decay->c[1];
  
  double x0=is->x[0];
  double x1=is->x[1];
  double y0=is->y[0];
  double y1=is->y[1];
  int i=0;

  while(i<n){
    double yd;
    if(y1<y0)y1=y0; // slope fixup

     yd = y0*d_c0+y1*d_c1;


    if(x[i]>yd)yd=x[i];

    x1=x0;x0=x[i];
    y1=y0;x[i]=y0=yd;
    i++;
  }
  
  is->x[0]=x0;
  is->x[1]=x1;
  is->y[0]=y0;
  is->y[1]=y1;
  
}

void compute_iir_decayonly2(float *x, int n, iir_state *is, 
			    iir_filter *decay){
  double d_c0=decay->c[0];
  double d_c1=decay->c[1];
  double d_g=decay->g;
  
  double x0=is->x[0];
  double x1=is->x[1];
  double y0=is->y[0];
  double y1=is->y[1];
  int i=0;

  while(i<n){
    double yd;

    if(y1<y0)y1=y0; // slope fixup

    yd = (x[i]+x0*2.+x1)/d_g + y0*d_c0+y1*d_c1;
    
    if(x[i]>yd)yd=x[i];
    
    x1=x0;x0=x[i];
    y1=y0;x[i]=y0=yd;
    i++;
  }
  
  is->x[0]=x0;
  is->x[1]=x1;
  is->y[0]=y0;
  is->y[1]=y1;
  
}

void compute_iir_freefall3(float *x, int n, iir_state *is, 
			  iir_filter *decay){
  double d_c0=decay->c[0];
  double d_c1=decay->c[1];
  double d_c2=decay->c[2];
  
  double x0=is->x[0],y0=is->y[0];
  double x1=is->x[1],y1=is->y[1];
  double x2=is->x[2],y2=is->y[2];
  int i=0;

  while(i<n){
    double yd;
    if(y1<y0)y1=y0; // slope fixup
    if(y2<y1)y2=y1; // slope fixup


     yd = y0*d_c0+y1*d_c1+y2*d_c2;


    if(x[i]>yd)yd=x[i];

    x2=x1;x1=x0;x0=x[i];
    y2=y1;y1=y0;x[i]=y0=yd;
    i++;
  }
  
  is->x[0]=x0;is->y[0]=y0;
  is->x[1]=x1;is->y[1]=y1;
  is->x[2]=x2;is->y[2]=y2;
  
}

void compute_iir_freefall4(float *x, int n, iir_state *is, 
			  iir_filter *decay){
  double d_c0=decay->c[0];
  double d_c1=decay->c[1];
  double d_c2=decay->c[2];
  double d_c3=decay->c[3];
  
  double x0=is->x[0],y0=is->y[0];
  double x1=is->x[1],y1=is->y[1];
  double x2=is->x[2],y2=is->y[2];
  double x3=is->x[3],y3=is->y[3];
  int i=0;

  while(i<n){
    double yd;
    if(y1<y0)y1=y0; // slope fixup
    if(y2<y1)y2=y1; // slope fixup
    if(y3<y2)y3=y2; // slope fixup


     yd = y0*d_c0+y1*d_c1+y2*d_c2+y3*d_c3;

    if(x[i]>yd)yd=x[i];

    x3=x2;x2=x1;x1=x0;x0=x[i];
    y3=y2;y2=y1;y1=y0;x[i]=y0=yd;
    i++;
  }
  
  is->x[0]=x0;is->y[0]=y0;
  is->x[1]=x1;is->y[1]=y1;
  is->x[2]=x2;is->y[2]=y2;
  is->x[3]=x3;is->y[3]=y3;
  
}

/* symmetric filter computation */

void compute_iir_symmetric2(float *x, int n, iir_state *is, 
			   iir_filter *filter){
  double c0=filter->c[0];
  double c1=filter->c[1];
  double g=filter->g;
  
  double x0=is->x[0];
  double x1=is->x[1];
  double y0=is->y[0];
  double y1=is->y[1];
    
  int i=0;
      
  while(i<n){
    double yd= (x[i]+x0*2.+x1)/g + y0*c0+y1*c1;
    x1=x0;x0=x[i];
    y1=y0;x[i]=y0=yd;
    i++;
  }
  
  is->x[0]=x0;
  is->x[1]=x1;
  is->y[0]=y0;
  is->y[1]=y1;
  
}

void compute_iir_symmetric3(float *x, int n, iir_state *is, 
			   iir_filter *filter){
  double c0=filter->c[0];
  double c1=filter->c[1];
  double c2=filter->c[2];
  double g=filter->g;
  
  double x0=is->x[0],y0=is->y[0];
  double x1=is->x[1],y1=is->y[1];
  double x2=is->x[2],y2=is->y[2];
    
  int i=0;
      
  while(i<n){
    double yd= (x[i]+(x0+x1)*3.+x2)/g + y0*c0+y1*c1+y2*c2;
    x2=x1;x1=x0;x0=x[i];
    y2=y1;y1=y0;x[i]=y0=yd;
    i++;
  }
  
  is->x[0]=x0;is->y[0]=y0;
  is->x[1]=x1;is->y[1]=y1;
  is->x[2]=x2;is->y[2]=y2;
  
}

void compute_iir_symmetric4(float *x, int n, iir_state *is, 
			   iir_filter *filter){
  double c0=filter->c[0];
  double c1=filter->c[1];
  double c2=filter->c[2];
  double c3=filter->c[3];
  double g=filter->g;
  
  double x0=is->x[0],y0=is->y[0];
  double x1=is->x[1],y1=is->y[1];
  double x2=is->x[2],y2=is->y[2];
  double x3=is->x[3],y3=is->y[3];
    
  int i=0;
      
  while(i<n){
    double yd= (x[i]+(x0+x2)*4.+x1*6.+x3)/g + 
      y0*c0+y1*c1+y2*c2+y3*c3;
    x3=x2;x2=x1;x1=x0;x0=x[i];
    y3=y2;y2=y1;y1=y0;x[i]=y0=yd;
    i++;
  }
  
  is->x[0]=x0;is->y[0]=y0;
  is->x[1]=x1;is->y[1]=y1;
  is->x[2]=x2;is->y[2]=y2;
  is->x[3]=x3;is->y[3]=y3;
  
}


/* filter decision wrapper */
void compute_iir2(float *x, int n, iir_state *is, 
		 iir_filter *attack, iir_filter *decay){

  if (attack->alpha > decay->alpha){
    /* fast attack, slow decay */
    compute_iir_fast_attack2(x, n, is, attack, decay);
  }else{
    compute_iir_symmetric2(x, n, is, attack);
  }
}


