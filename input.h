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

extern void input_Acursor_set(off_t c);
extern void input_Bcursor_set(off_t c);
extern off_t input_time_to_cursor(const char *t);
extern void input_cursor_to_time(off_t cursor,char *t);
extern void time_fix(char *buffer);
extern off_t input_seek(off_t pos);
extern time_linkage *input_read(void);
extern void input_parse(char *filename,int newgroup);
extern int input_load(void);
extern int pull_input_feedback(float *peak,float *rms,off_t *cursor);
extern void input_reset(void);
extern off_t input_time_seek_rel(float s);
