/*
 *
 *  postfish
 *    
 *      Copyright (C) 2002-2004 Monty and Xiph.Org
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

#include <sys/types.h>
#include "postfish.h"
#include "mute.h"


sig_atomic_t *mute_active;
extern int input_ch;

int mute_load(void){
  mute_active=calloc(input_ch,sizeof(*mute_active));
  return 0;
}

int mute_channel_muted(u_int32_t active,int i){
  return(!(active&(1<<i)));
}

time_linkage *mute_read(time_linkage *in){
  u_int32_t val=0;
  int i;

  for(i=0;i<input_ch;i++)
    if(!mute_active[i])
      val|= (1<<i);

  in->active=val;
  return(in);
}
