/*
 *
 *  form.h
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

enum field_type { FORM_TIME, FORM_DB, FORM_P2 };

typedef struct {
  enum field_type type;
  int x;
  int y;
  int width;
  int editwidth;
  int editgroup;

  int min;
  int max;
  int dpoint;
  
  void *var;
  int  *flag;
  struct form *form;

  int cursor;
} formfield;

typedef struct form {
  formfield *fields;
  int count;
  int storage;

  int cursor;
  int editable;
} form;

extern void form_init(form *f,int maxstorage,int editable);
extern void form_clear(form *f);
extern void draw_field(formfield *f);
extern void form_redraw(form *f);
extern formfield *field_add(form *f,enum field_type type,int x,int y,
			    int width,int group,void *var,int *flag,
			    int d,int min, int max);
extern int form_handle_char(form *f,int c);
