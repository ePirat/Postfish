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

typedef struct {
  sig_atomic_t panel_active;
  sig_atomic_t panel_visible;

  sig_atomic_t time;     /* 1-1000 */
  sig_atomic_t damping;  /* 0.-1. * 1000 */ 
  sig_atomic_t wet;      /* 0.-1. * 1000 */
} plate_set;

extern void plate_reset(void);
extern int plate_load(int outch);
extern time_linkage *plate_read_channel(time_linkage *in,
					time_linkage **revA,
					time_linkage **revB);
extern time_linkage *plate_read_master(time_linkage *in);

extern plate_set *plate_channel_set;
extern plate_set plate_master_set;

