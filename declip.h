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

extern int declip_load(void);
extern int declip_reset(void);
extern time_linkage *declip_read(time_linkage *in);
extern int pull_declip_feedback(int *clip,float *peak,int *total);

extern sig_atomic_t *declip_active;
extern sig_atomic_t declip_pending_blocksize;
extern sig_atomic_t *declip_chtrigger;
extern sig_atomic_t declip_convergence;
extern sig_atomic_t declip_iterations;
extern sig_atomic_t declip_visible;

