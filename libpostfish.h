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

#ifndef _LIBPOSTFISH_H_

struct postfish_instance;
typedef struct postfish_instance postfish_instance;

extern void time_linkage_init(time_linkage *new,int ch);
extern int time_linkage_copy(time_linkage *dest,time_linkage *src);
extern int time_linkage_channels(time_linkage *in);
extern int time_linkage_samples(time_linkage *in);
extern int time_linkage_init_alias_split(time_linkage *in,time_linkage *out);
extern void time_linkage_init_alias_combine(time_linkage *in,time_linkage *out,int ch);
extern void time_linkage_swap(time_linkage *a, time_linkage *b);
extern void time_linkage_clear(time_linkage *in);

#endif
