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

typedef struct feedback_generic{
  struct feedback_generic *next;
} feedback_generic;

typedef struct feedback_generic_pool{
  feedback_generic *feedback_list_head;
  feedback_generic *feedback_list_tail;
  feedback_generic *feedback_pool;
} feedback_generic_pool;

extern feedback_generic *feedback_new(feedback_generic_pool *pool,
				      feedback_generic *(*constructor)(void));
extern void feedback_push(feedback_generic_pool *pool,
			  feedback_generic *f);
extern feedback_generic *feedback_pull(feedback_generic_pool *pool);
extern void feedback_old(feedback_generic_pool *pool,
			 feedback_generic *f);
extern int feedback_deep(feedback_generic_pool *pool);

