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

#if 0
extern void ef_free(void * address);
extern void *ef_realloc(void * oldBuffer, size_t newSize);
extern void *ef_malloc(size_t size);
extern void *ef_calloc(size_t nelem, size_t elsize);
extern void *ef_valloc (size_t size);

#define free ef_free
#define realloc ef_realloc
#define malloc ef_malloc
#define calloc ef_calloc
#define valloc ef_valloc
#endif 

#define OUTPUT_CHANNELS 8     // UI code assumes this is <=8
#define MAX_INPUT_CHANNELS 32 // engine code requires <= 32 

static inline float todB(float x){
  return logf((x)*(x)+1e-30f)*4.34294480f;
}

static inline double todBd(double x){
  return log((x)*(x)+1e-30)*4.34294480;
}

static inline float fromdB(float x){
  return expf((x)*.11512925f);
}

static inline int zerome(double x){
  return (x*x < 1.e-30);
}

#ifdef UGLY_IEEE754_FLOAT32_HACK

static inline float todB_a(float x){
  return (float)((*((int32_t *)&x))&0x7fffffff) * 7.17711438e-7f -764.6161886f;
}

// eliminate a *.5 in ops on sq magnitudes
static inline float todB_a2(float x){
  return (float)((*((int32_t *)&x))&0x7fffffff) * 3.58855719e-7f -382.3080943f;
}

static inline float fromdB_a(float x){
  int y=(x < -300 ? 0 : 1.39331762961e+06f*(x+764.6161886f));
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

#ifndef max
#define max(x,y) ((x)>(y)?(x):(y))
#endif


#define toOC(n)     (log(n)*1.442695f-5.965784f)
#define fromOC(o)   (exp(((o)+5.965784f)*.693147f))
#define toBark(n)   (13.1f*atan(.00074f*(n))+2.24f*atan((n)*(n)*1.85e-8f)+1e-4f*(n))
#define fromBark(z) (102.f*(z)-2.f*pow(z,2.f)+.4f*pow(z,3.f)+pow(1.46f,z)-1.f)

typedef struct time_linkage {
  int samples;  /* normally same as size; exception is EOF */
  int channels;
  float **data;
  u_int32_t active; /* active channel bitmask */
} time_linkage;

extern sig_atomic_t loop_active;
extern sig_atomic_t playback_active;
extern sig_atomic_t playback_exit;
extern sig_atomic_t playback_seeking;
extern sig_atomic_t master_att;
extern int outfileno;
extern int input_seekable;
extern int eventpipe[2];
extern int input_ch;
extern int input_size;
extern int input_rate;

extern int mute_channel_muted(u_int32_t bitmap,int i);
extern void clean_exit(int sig);
#endif

