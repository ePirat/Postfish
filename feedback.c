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

#include "postfish.h"
#include "feedback.h"

static pthread_mutex_t feedback_mutex=PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

feedback_generic *feedback_new(feedback_generic_pool *pool,
			       feedback_generic *(*constructor)(void)){
  feedback_generic *ret;
  
  pthread_mutex_lock(&feedback_mutex);
  if(pool->feedback_pool){
    ret=pool->feedback_pool;
    pool->feedback_pool=pool->feedback_pool->next;
    pthread_mutex_unlock(&feedback_mutex);
    return ret;
  }
  pthread_mutex_unlock(&feedback_mutex);

  ret=constructor();
  return ret;
}

void feedback_push(feedback_generic_pool *pool,
		   feedback_generic *f){
  f->next=NULL;

  pthread_mutex_lock(&feedback_mutex);
  if(!pool->feedback_list_tail){
    pool->feedback_list_tail=f;
    pool->feedback_list_head=f;
  }else{
    pool->feedback_list_head->next=f;
    pool->feedback_list_head=f;
  }
  pthread_mutex_unlock(&feedback_mutex);
}

feedback_generic *feedback_pull(feedback_generic_pool *pool){
  feedback_generic *f;

  pthread_mutex_lock(&feedback_mutex);
  if(pool->feedback_list_tail){
    
    f=pool->feedback_list_tail;
    pool->feedback_list_tail=pool->feedback_list_tail->next;
    if(!pool->feedback_list_tail)pool->feedback_list_head=0;

  }else{
    pthread_mutex_unlock(&feedback_mutex);
    return 0;
  }
  pthread_mutex_unlock(&feedback_mutex);
  return(f);
}

void feedback_old(feedback_generic_pool *pool,
		  feedback_generic *f){
  
  pthread_mutex_lock(&feedback_mutex);
  f->next=pool->feedback_pool;
  pool->feedback_pool=f;
  pthread_mutex_unlock(&feedback_mutex);
}

/* are there multiple feedback outputs waiting or just one (a metric
   of 'are we behind?') */
int feedback_deep(feedback_generic_pool *pool){
  if(pool){
    pthread_mutex_lock(&feedback_mutex);
    if(pool->feedback_list_tail)
      if(pool->feedback_list_tail->next){
	pthread_mutex_unlock(&feedback_mutex);
	return 1;
      }
    pthread_mutex_unlock(&feedback_mutex);
  }
  return 0;
}
