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

typedef struct{
  GtkWidget *mainpanel_windowbutton;
  GtkWidget **mainpanel_activebutton;
  GtkWidget *subpanel_windowbutton;
  GtkWidget **subpanel_activebutton;
  GtkWidget *subpanel_toplevel;
  GtkWidget *subpanel_topframe;
  GtkWidget *subpanel_box;
  sig_atomic_t *activevar;

  int active_button_count; /* silliness around the rotating non-alt-shortcut */
  int active_button_start; /* silliness around the rotating non-alt-shortcut */

  sig_atomic_t *mappedvar;

  postfish_mainpanel *mainpanel;
} subpanel_generic;

extern subpanel_generic *subpanel_create(postfish_mainpanel *mp,
					 GtkWidget *windowbutton,
					 GtkWidget **activebutton,
					 sig_atomic_t *activevar,
					 sig_atomic_t *mappedvar,
					 char *prompt,char **shortcut,
					 int start,int num);

extern void subpanel_show_all_but_toplevel(subpanel_generic *s);
