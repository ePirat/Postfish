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

extern const char *config_get_string(char *key,int bank, int A, int B, int C);
extern int config_get_integer(char *key,int bank, int A, int B, int C,int valnum, int *val);
extern int config_get_sigat(char *key,int bank, int A, int B, int C,int valnum, sig_atomic_t *val);
extern int config_get_vector(char *key,int bank, int A, int B, int C,int n, sig_atomic_t *v);
extern void config_set_string(char *key,int bank, int A, int B, int C, const char *s);
extern void config_set_vector(char *key,int bank, int A, int B, int C,int n, sig_atomic_t *v);
extern void config_set_integer(char *key,int bank, int A, int B, int C, int valnum, int val);
extern int config_load(char *filename);
extern void config_save(char *filename);
