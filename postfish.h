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

#define _GNU_SOURCE
#define _LARGEFILE_SOURCE 
#define _LARGEFILE64_SOURCE
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

#define todB(x)   ((x)==0?-400.f:log((x)*(x))*4.34294480f)
#define fromdB(x) (exp((x)*.11512925f))  
#define toOC(n)     (log(n)*1.442695f-5.965784f)

typedef struct time_linkage {
  int size;
  int samples;  /* normally same as size; exception is EOF */
  int channels;
  int rate;
  double **data;
} time_linkage;

extern pthread_mutex_t master_mutex;

extern sig_atomic_t loop_active;
extern sig_atomic_t playback_active;
extern sig_atomic_t playback_exit;
extern sig_atomic_t master_att;
extern int outfileno;
extern int seekable;
extern int eventpipe[2];
extern int input_ch;

