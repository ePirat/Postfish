/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE OggVorbis SOFTWARE CODEC SOURCE CODE.   *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS LIBRARY SOURCE IS     *
 * GOVERNED BY A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS SOURCE *
 * IN 'COPYING'. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.       *
 *                                                                  *
 * THE OggVorbis SOURCE CODE IS (C) COPYRIGHT 1994-2002             *
 * by the XIPHOPHORUS Company http://www.xiph.org/                  *
 *                                                                  *
 ********************************************************************

  function: LPC low level routines
  last mod: $Id: lpc.h,v 1.1 2003/10/20 03:08:43 xiphmont Exp $

 ********************************************************************/

#ifndef _V_LPC_H_
#define _V_LPC_H_

#include "vorbis/codec.h"


extern double lpc_from_data(double *data,double *lpc,int n,int m);
extern double lpc_from_incomplete_data(double *data,double *lpc,int n,int m);
extern void lpc_extrapolate(double *lpc,double *prime,int m,
			    double *data,int n);
extern void lpc_subtract(double *lpc,double *prime,int m,
			 double *data,int n);


#endif
