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

#include "postfish.h"
#include "window.h"

static pthread_mutex_t window_mutex=PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static float ***window_func=0; /* sin(), sin()^2, sin(sin()^2),sin(sin^2)^2 
				  
				  1,2,4,8,16,32,64,128,256,512,1024,
				  2048,4096,8192,16384,32768,65536 */

/* type is one of 0==sin(x),
                  1==sin(x)^2,
		  2==sin(sin(x)^2),
		  3==sin(sin(x)^2)^2 */
/* returns quarter-cycle (left half) n+1 samples of a window from 0. to 1. */
static int ilog(long x){
  int ret=-1;

  while(x>0){
    x>>=1;
    ret++;
  }
  return ret;
}

float *window_get(int type,int n){
  int bits=ilog(n),i;
  if((1<<bits)!=n)return 0;
  
  if(bits<0)return 0;
  if(bits>=17)return 0;
  
  if(type<0)return 0;
  if(type>3)return 0;

  pthread_mutex_lock(&window_mutex);
  if(!window_func)
    window_func=calloc(4,sizeof(*window_func));

  if(!window_func[type])
    window_func[type]=calloc(17,sizeof(**window_func));

  if(!window_func[type][bits]){
    window_func[type][bits]=malloc((n+1)*sizeof(***window_func));
    for(i=0;i<n+1;i++)window_func[type][bits][i]=sin(M_PIl*.5*i/n);
    if(type>0)
      for(i=0;i<n+1;i++)window_func[type][bits][i]*=
			   window_func[type][bits][i];
    if(type>1)
      for(i=0;i<n+1;i++)window_func[type][bits][i]=
			   sin(window_func[type][bits][i]*M_PIl*.5);
    if(type>2)
      for(i=0;i<n+1;i++)window_func[type][bits][i]*=
			   window_func[type][bits][i];
  }

  pthread_mutex_unlock(&window_mutex);
  return window_func[type][bits];
}

void window_apply(float *data, float *window, float scale, int halfn){
  float *data2=data+halfn*2;
  int i;

  *(data++) *= window[0]*scale;
  for(i=1;i<halfn;i++){      
    float val=window[i]*scale;
    *(data++) *= val;
    *(--data2)*= val;
  }
  *(data++) *= window[i]*scale;
}
