/*
 *
 *  postfish
 *    
 *      Copyright (C) 2002-2005 Monty
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
#include "internal.h"

void time_linkage_init(time_linkage *new,int ch){
  int i;

  new->samples=0;
  new->channels=ch;
  new->active=0;
  new->alias=0;
  new->data=malloc(ch*sizeof(*new->data));

  for(i=0;i<ch;i++)
    new->data[i]=malloc(input_size*sizeof(*new->data));
}

int time_linkage_channels(time_linkage *in){
  return in->channels;
}

int time_linkage_samples(time_linkage *in){
  return in->samples;
}

/* in: pointer to single initialized time_linkage structure
   out: pointer to array of (in->channels) time_linkage structures */

int time_linkage_init_alias_split(time_linkage *in,time_linkage *out){
  int i;
  int ch=in->channels;

  for(i=0;i<ch;i++){
    out[i].samples=0;
    out[i].channels=1;
    out[i].active=0;
    out[i].alias=1;
    out[i].data=malloc(sizeof(*out->data));
    out[i].data[0]=in->data[i];
  }
  return 0;
}

/* in: pointer to array of ch initialized time_linkage structs
   out: pointer to single uninitialized time_linkage struct
   ch: number of input linkages, number of channels in output linkage */

void time_linkage_init_alias_combine(time_linkage *in,time_linkage *out,int ch){
  int i;

  out->alias=1;
  out->samples=0;
  out->channels=ch;
  out->active=0;
  out->data=malloc(ch*sizeof(*out->data));

  for(i=0;i<ch;i++)
    out->data[i]=in[i].data[0];
}

void time_linkage_swap(time_linkage *a, time_linkage *b){
  float **dtmp=a->data;
  int ctmp=a->channels;
  int stmp=a->samples;
  u_int32_t atmp=a->active;

  a->data=b->data;
  b->data=dtmp;

  a->channels=b->channels;
  b->channels=ctmp;

  a->samples=b->samples;
  b->samples=stmp;

  a->active=b->active;
  b->active=atmp;
}

void time_linkage_clear(time_linkage *in){
  int i;
  for(i=0;i<in->channels;i++)
    memset(in->data[i],0,sizeof(**in->data)*input_size);
  in->samples=0;
  in->active=0;
}

int time_linkage_copy(time_linkage *dest,time_linkage *src){
  int i;
  if(dest->channels != src->channels)return -1;
  
  for(i=0;i<dest->channels;i++)
    memcpy(dest->data[i],src->data[i],input_size*sizeof(**src->data));
  dest->samples=src->samples;
  dest->active=src->active;

  return 0;
}
