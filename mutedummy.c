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

#include "postfish.h"
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "readout.h"
#include "multibar.h"
#include "mainpanel.h"
#include "mutedummy.h"

extern sig_atomic_t *mute_active;
extern int input_ch;

static int activebutton_action(GtkWidget *widget,gpointer in){
  int num=(int)in;
  int active=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  mute_active[num]=active;
  
  return FALSE;
}

void mutedummy_create(postfish_mainpanel *mp,
		      GtkWidget **windowbutton,
		      GtkWidget **activebutton){
  int i;

  /* nothing to do here but slap an activation callback on each activebutton */
  for(i=0;i<input_ch;i++)
    g_signal_connect_after (G_OBJECT (activebutton[i]), "clicked",
			    G_CALLBACK (activebutton_action), (gpointer)i);
    
}
