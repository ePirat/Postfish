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
#include "subpanel.h"
#include "output.h"
#include "outpanel.h"
#include "config.h"

typedef struct {
  GtkWidget *source[OUTPUT_CHANNELS];
  GtkWidget *device;
  GtkWidget *format;
  GtkWidget *ch;
  GtkWidget *depth;
} outsub_state;

typedef struct {
  GtkWidget *monitor_active;
  GtkWidget *stdout_active;

  outsub_state monitor;
  outsub_state stdout;
} outpanel_state;

outpanel_state state;

void outpanel_state_to_config(int bank){
  config_set_vector("output_active",bank,0,0,0,2,outset.panel_active);

  config_set_vector("output_monitor_source",bank,0,0,0,OUTPUT_CHANNELS,outset.monitor.source);
  config_set_integer("output_monitor_set",bank,0,0,0,0,outset.monitor.device);
  config_set_integer("output_monitor_set",bank,0,0,0,1,outset.monitor.bytes);
  config_set_integer("output_monitor_set",bank,0,0,0,2,outset.monitor.ch);
  config_set_integer("output_monitor_set",bank,0,0,0,3,outset.monitor.format);

  config_set_vector("output_stdout_source",bank,0,0,0,OUTPUT_CHANNELS,outset.stdout.source);
  config_set_integer("output_stdout_set",bank,0,0,0,0,outset.stdout.device);
  config_set_integer("output_stdout_set",bank,0,0,0,1,outset.stdout.bytes);
  config_set_integer("output_stdout_set",bank,0,0,0,2,outset.stdout.ch);
  config_set_integer("output_stdout_set",bank,0,0,0,3,outset.stdout.format);
}

void outpanel_state_from_config(int bank){
  int i;

  config_get_vector("output_active",bank,0,0,0,2,outset.panel_active);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state.monitor_active),outset.panel_active[0]);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state.stdout_active),outset.panel_active[1]);

  config_get_vector("output_monitor_source",bank,0,0,0,OUTPUT_CHANNELS,outset.monitor.source);
  for(i=0;i<OUTPUT_CHANNELS;i++)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state.monitor.source[i]),
				 outset.monitor.source[i]);

  config_get_sigat("output_monitor_set",bank,0,0,0,0,&outset.monitor.device);

  /* don't set a device that doesn't exist */
  if(state.monitor.device && outset.monitor.device<monitor_entries)
    gtk_combo_box_set_active(GTK_COMBO_BOX(state.monitor.device),outset.monitor.device);

  config_get_sigat("output_monitor_set",bank,0,0,0,1,&outset.monitor.bytes);
  if(state.monitor.depth)
    gtk_combo_box_set_active(GTK_COMBO_BOX(state.monitor.depth),outset.monitor.bytes);
  config_get_sigat("output_monitor_set",bank,0,0,0,2,&outset.monitor.ch);
  if(state.monitor.ch)
    gtk_combo_box_set_active(GTK_COMBO_BOX(state.monitor.ch),outset.monitor.ch);
  config_get_sigat("output_monitor_set",bank,0,0,0,3,&outset.monitor.format);
  if(state.monitor.format)
    gtk_combo_box_set_active(GTK_COMBO_BOX(state.monitor.format),outset.monitor.format);


  config_get_vector("output_stdout_source",bank,0,0,0,OUTPUT_CHANNELS,outset.stdout.source);
  for(i=0;i<OUTPUT_CHANNELS;i++)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state.stdout.source[i]),
				 outset.stdout.source[i]);

  config_get_sigat("output_stdout_set",bank,0,0,0,0,&outset.stdout.device);
  if(state.stdout.device)
    gtk_combo_box_set_active(GTK_COMBO_BOX(state.stdout.device),outset.stdout.device);
  config_get_sigat("output_stdout_set",bank,0,0,0,1,&outset.stdout.bytes);
  if(state.stdout.depth)
    gtk_combo_box_set_active(GTK_COMBO_BOX(state.stdout.depth),outset.stdout.bytes);
  config_get_sigat("output_stdout_set",bank,0,0,0,2,&outset.stdout.ch);
  if(state.stdout.ch)
    gtk_combo_box_set_active(GTK_COMBO_BOX(state.stdout.ch),outset.stdout.ch);
  config_get_sigat("output_stdout_set",bank,0,0,0,3,&outset.stdout.format);
  if(state.stdout.format)
    gtk_combo_box_set_active(GTK_COMBO_BOX(state.stdout.format),outset.stdout.format);


}

static void menuchange(GtkWidget *w,gpointer in){
  sig_atomic_t *var=(sig_atomic_t *)in;

  *var=gtk_combo_box_get_active(GTK_COMBO_BOX(w));
}

static void buttonchange(GtkWidget *w,gpointer in){
  sig_atomic_t *var=(sig_atomic_t *)in;

  *var=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
}


static GtkWidget *outpanel_subpanel(postfish_mainpanel *mp,
				    subpanel_generic *panel, 
				    output_subsetting *set,
				    char *prompt,
				    int devp,
				    int active,
				    outsub_state *os){

  GtkWidget *frame=gtk_frame_new(prompt);
  GtkWidget *table=gtk_table_new(4,3,0);
  
  GtkWidget *l1=gtk_label_new("active channels ");
  GtkWidget *l2=gtk_label_new(devp?"output device ":"output format ");
  GtkWidget *l3=gtk_label_new("output channels ");
  GtkWidget *l4=gtk_label_new("output bit depth ");

  GtkWidget *b1=gtk_hbox_new(1,0);
  GtkWidget *b2=gtk_hbox_new(1,0);
  GtkWidget *b3=gtk_hbox_new(1,0);
  GtkWidget *b4=gtk_hbox_new(1,0);

  gtk_frame_set_shadow_type(GTK_FRAME(frame),GTK_SHADOW_ETCHED_IN);
  gtk_container_set_border_width(GTK_CONTAINER(table),4);

  gtk_misc_set_alignment(GTK_MISC(l1),1.,.5);
  gtk_misc_set_alignment(GTK_MISC(l2),1.,.5);
  gtk_misc_set_alignment(GTK_MISC(l3),1.,.5);
  gtk_misc_set_alignment(GTK_MISC(l4),1.,.5);

  gtk_table_attach_defaults(GTK_TABLE(table),l1,0,1,0,1);
  gtk_table_attach_defaults(GTK_TABLE(table),l2,0,1,1,2);
  gtk_table_attach_defaults(GTK_TABLE(table),l3,0,1,2,3);
  gtk_table_attach_defaults(GTK_TABLE(table),l4,0,1,3,4);

  /* channel buttonx0rz */
  {
    int i;
    char buffer[80];
    for(i=0;i<OUTPUT_CHANNELS;i++){
      GtkWidget *b;
      sprintf(buffer," %d ",i+1);

      b=gtk_toggle_button_new_with_label(buffer);
      
      gtk_box_pack_start(GTK_BOX(b1),b,0,0,0);
      // careful; this breaks (cosmetically) for OUTPUT_CHANNELS > 8
      gtk_widget_add_accelerator (b, "activate", mp->group, GDK_1+i, 0, 0); 
      gtk_widget_set_sensitive(b,active);
      g_signal_connect (G_OBJECT (b), "clicked",
			G_CALLBACK (buttonchange), &set->source[i]);

      if(i<2)gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b),1);
      os->source[i]=b;
    }
    gtk_table_attach_defaults(GTK_TABLE(table),b1,1,3,0,1);
  }


  {
    int i;
    GtkWidget *menu=gtk_combo_box_new_text();
    
    if(devp){
      /* device selection */
      for(i=0;i<monitor_entries;i++)
	gtk_combo_box_append_text (GTK_COMBO_BOX (menu), monitor_list[i].name);
      
      if(i==0)
	gtk_combo_box_append_text (GTK_COMBO_BOX (menu), "stdout");

      g_signal_connect (G_OBJECT (menu), "changed",
			G_CALLBACK (menuchange), &set->device);
      
      os->device=menu;
      os->format=0;
    }else{
      
      /* format selection */
      int i;
      char *formats[]={"WAV","AIFF-C","raw (little endian)","raw (big endian)"};
      
      for(i=0;i<4;i++)
	gtk_combo_box_append_text (GTK_COMBO_BOX (menu), formats[i]);

      g_signal_connect (G_OBJECT (menu), "changed",
			G_CALLBACK (menuchange), &set->format);
      os->format=menu;
      os->device=0;
    }

    gtk_combo_box_set_active(GTK_COMBO_BOX(menu),0);
    
    gtk_box_pack_start(GTK_BOX(b2),menu,1,1,0);

    gtk_widget_set_sensitive(menu,active);

  }
  gtk_table_attach(GTK_TABLE(table),b2,1,2,1,2,GTK_FILL,0,0,0);


  {
    /* channels selection */
    int i;
    GtkWidget *menu=gtk_combo_box_new_text();
    char *formats[]={"auto","1","2","4","6","8"};
    
    for(i=0;i<6;i++)
      gtk_combo_box_append_text (GTK_COMBO_BOX (menu), formats[i]);

    gtk_combo_box_set_active(GTK_COMBO_BOX(menu),0);
    
    gtk_box_pack_start(GTK_BOX(b3),menu,1,1,0);
    gtk_widget_set_sensitive(menu,active);
    g_signal_connect (G_OBJECT (menu), "changed",
		      G_CALLBACK (menuchange), &set->ch);
    os->ch=menu;
  }
  gtk_table_attach(GTK_TABLE(table),b3,1,2,2,3,GTK_FILL,0,0,0);
  

  {
    /* bit depth selection */
    int i;
    GtkWidget *menu=gtk_combo_box_new_text();
    char *formats[]={"auto","8","16","24"};
    
    for(i=0;i<4;i++)
      gtk_combo_box_append_text (GTK_COMBO_BOX (menu), formats[i]);

    gtk_combo_box_set_active(GTK_COMBO_BOX(menu),0);
    
    gtk_box_pack_start(GTK_BOX(b4),menu,1,1,0);
    gtk_widget_set_sensitive(menu,active);

    g_signal_connect (G_OBJECT (menu), "changed",
		      G_CALLBACK (menuchange), &set->bytes);

    os->depth=menu;
  }
  gtk_table_attach(GTK_TABLE(table),b4,1,2,3,4,GTK_FILL,0,0,0);

  gtk_table_set_row_spacing(GTK_TABLE(table),0,5);

  gtk_container_add(GTK_CONTAINER(frame),table);
  return frame;
}

void outpanel_create(postfish_mainpanel *mp,
		      GtkWidget *windowbutton,
		      GtkWidget **activebutton){

  char *shortcuts[]={"mOn"," o "};

  subpanel_generic *panel=subpanel_create(mp,windowbutton,activebutton,
					  outset.panel_active,
					  &outset.panel_visible,
					  "_Output configuration",shortcuts,
					  0,2);
  
  GtkWidget *box=gtk_hbox_new(1,0);
  GtkWidget *monitor_panel=outpanel_subpanel(mp,panel,&outset.monitor,
					     " audio monitor output ",
					     1,output_monitor_available,
					     &state.monitor);
  GtkWidget *stdout_panel=outpanel_subpanel(mp,panel,&outset.stdout,
					    " standard output ",
					    output_stdout_device,
					    output_stdout_available,
					    &state.stdout);
  
  gtk_box_pack_start(GTK_BOX(box),monitor_panel,0,0,0);
  gtk_box_pack_start(GTK_BOX(box),stdout_panel,0,0,2);

  if(!output_monitor_available)gtk_widget_set_sensitive(activebutton[0],0);
  if(!output_monitor_available)gtk_widget_set_sensitive(panel->subpanel_activebutton[0],0);
  if(!output_stdout_available)gtk_widget_set_sensitive(activebutton[1],0);
  if(!output_stdout_available)gtk_widget_set_sensitive(panel->subpanel_activebutton[1],0);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(activebutton[0]),1);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(activebutton[1]),1);

  gtk_container_add(GTK_CONTAINER(panel->subpanel_box),box);
  subpanel_show_all_but_toplevel(panel);

  state.monitor_active=activebutton[0];
  state.stdout_active=activebutton[1];
}

void outpanel_monitor_off(){
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state.monitor_active),0);
}

void outpanel_stdout_off(){
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state.stdout_active),0);
}
