/*
 *
 *  postfish
 *    
 *      Copyright (C) 2002-2005 Monty and Xiph.Org
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
#include "mix.h"
#include "mute.h"
#include "window.h"

extern int input_ch;
extern sig_atomic_t *mixpanel_active;

typedef struct {
  u_int32_t active_prev;
  int active_ch;
  int init_state;
  float *leftwindow;
} mute_state;

static mute_state channel_state;

int mute_load(void){
  memset(&channel_state,0,sizeof(channel_state));
  channel_state.active_ch=input_ch;
  channel_state.leftwindow=window_get(1,input_size);
  return 0;
}

int mute_channel_muted(u_int32_t active,int i){
  return(!(active&(1<<i)));
}

time_linkage *mute_read(time_linkage *in){
  u_int32_t preval=0,retval=0;
  int i,j;
  
  if(!channel_state.init_state)
    channel_state.active_prev=in->active;

  /* the mute module is responsible for smoothly ramping audio levels
     to/from zero and unity upon mute state change */

  for(i=0;i<input_ch;i++)
    if(mixpanel_active[i])
      preval|= (1<<i);

  /* the mute module is responsible for smoothly ramping audio levels
     to/from zero and unity upon mute state change */
  for(i=0;i<input_ch;i++){
    float *x=in->data[i];
    if(mixpanel_active[i]){
      retval|= (1<<i);

      if(mute_channel_muted(channel_state.active_prev,i)){
	/* mute->active */
	for(j=0;j<input_size;j++)
	  x[j]*=channel_state.leftwindow[j];
	
      }
    }else{
      if(!mute_channel_muted(channel_state.active_prev,i)){
	/* active->mute; ramp to zero and temporarily keep this
           channel active for this frame while ramping */

	retval|= (1<<i);
	for(j=0;j<input_size;j++)
	  x[j]*=channel_state.leftwindow[input_size-j]; 
	
      }
    }
  }

  in->active=retval;
  channel_state.active_prev=preval;
  channel_state.init_state=1;
  return(in);
}
