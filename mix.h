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

#include "postfish.h"

#define MIX_BLOCKS 4

typedef struct {
  sig_atomic_t master_att;  
  sig_atomic_t master_delay;

  sig_atomic_t placer_destA[OUTPUT_CHANNELS];
  sig_atomic_t placer_destB[OUTPUT_CHANNELS];
  sig_atomic_t placer_place;
  sig_atomic_t placer_att;
  sig_atomic_t placer_delay;

  sig_atomic_t insert_source[MIX_BLOCKS][3];
  sig_atomic_t insert_invert[MIX_BLOCKS];
  sig_atomic_t insert_att[MIX_BLOCKS];
  sig_atomic_t insert_delay[MIX_BLOCKS];
  sig_atomic_t insert_dest[MIX_BLOCKS][OUTPUT_CHANNELS];
} mix_settings;

extern int mix_load(int outch);
extern int mix_reset(void);
extern time_linkage *mix_read(time_linkage *in, 
			      time_linkage *inA,  // reverb channel 
			      time_linkage *inB); // reverb channel
extern int pull_mix_feedback(float **peak,float **rms);

