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

/* This project is a small, tightly tailored application.  it is not
   designed to be nigh-infinitely extensible, nor is it designed to be
   reusable code.  It's monolithic, inflexible, and designed that way
   on purpose. */

#ifndef _POSTFISH_H_
#define _POSTFISH_H_

#define _GNU_SOURCE
#define _ISOC99_SOURCE
#define _FILE_OFFSET_BITS 64
#define _REENTRANT 1
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#define __USE_GNU 1
#include <pthread.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <fcntl.h>

static inline float todB(float x){
  return logf((x)*(x)+1e-30f)*4.34294480f;
}

static inline float fromdB(float x){
  return expf((x)*.11512925f);
}

#ifdef UGLY_IEEE754_FLOAT32_HACK

static inline float todB_a(const float *x){
  return (float)((*(int32_t *)x)&0x7fffffff) * 7.1771144e-7f -764.27118f;
}

static inline float fromdB_a(float x){
  int y=1.3933e+06f*(x+764.27118f);
  return *(float *)&y;
}

#else

static inline float todB_a(const float *x){
  return todB(*x);
}

static inline float fromdB_a(float x){
  return fromdB(x);
}

#endif

#define toOC(n)     (log(n)*1.442695f-5.965784f)
#define fromOC(o)   (exp(((o)+5.965784f)*.693147f))
#define toBark(n)   (13.1f*atan(.00074f*(n))+2.24f*atan((n)*(n)*1.85e-8f)+1e-4f*(n))
#define fromBark(z) (102.f*(z)-2.f*pow(z,2.f)+.4f*pow(z,3.f)+pow(1.46f,z)-1.f)

typedef struct time_linkage {
  int size;
  int samples;  /* normally same as size; exception is EOF */
  int channels;
  int rate;
  float **data;
  u_int32_t active; /* active channel bitmask */
} time_linkage;

extern pthread_mutex_t master_mutex;

extern sig_atomic_t loop_active;
extern sig_atomic_t playback_active;
extern sig_atomic_t playback_exit;
extern sig_atomic_t playback_seeking;
extern sig_atomic_t master_att;
extern int outfileno;
extern int seekable;
extern int eventpipe[2];
extern int input_ch;

extern void mainpanel_go(int n,char *list[],int ch);
extern int mute_channel_muted(u_int32_t bitmap,int i);

#endif

