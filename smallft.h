/*
 *
 *  smallft.h
 *    
 *      Copyright (C) 2002 Monty
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
  int n;
  double *trigcache;
  int *splitcache;
} drft_lookup;

extern void drft_forward(drft_lookup *l,double *data);
extern void drft_backward(drft_lookup *l,double *data);
extern void drft_init(drft_lookup *l,int n);
extern void drft_clear(drft_lookup *l);
