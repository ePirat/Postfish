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
   is only the minimum code needed to build an arbitrary 2nd order
   Bessel filter */

#include "postfish.h"

#define TWOPI	    (2.0 * M_PIl)
#define EPS	    1e-10
#define MAXORDER    2
#define MAXPZ	    4

typedef struct { 
  double re, im;
} complex;

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

double mkbessel_2(double raw_alpha,double *ycoeff0,double *ycoeff1){ 
  int i;
  pzrep splane, zplane;
  complex topcoeffs[MAXPZ+1], botcoeffs[MAXPZ+1];
  double warped_alpha;
  complex dc_gain;

  memset(&splane,0,sizeof(splane));
  memset(&zplane,0,sizeof(zplane));

  splane.poles[splane.numpoles++] = (complex){ -1.10160133059e+00, 6.36009824757e-01};
  splane.poles[splane.numpoles++] = (complex){ -1.10160133059e+00, -6.36009824757e-01};
  
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

  *ycoeff0 = -(botcoeffs[0].re / botcoeffs[zplane.numpoles].re);
  *ycoeff1 = -(botcoeffs[1].re / botcoeffs[zplane.numpoles].re);

  return hypot(dc_gain.re,dc_gain.im);
}



