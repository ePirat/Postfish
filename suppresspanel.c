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
#include "feedback.h"
#include "suppress.h"
#include "suppresspanel.h"

extern sig_atomic_t suppress_active;
extern sig_atomic_t suppress_visible;
extern int input_ch;
extern int input_size;
extern int input_rate;

extern suppress_settings sset;

typedef struct {
  GtkWidget *cslider;
  Readout *readoutc;
  int number;
} tbar;

Readout *readoutsmooth;
Readout *readouttrigger;
Readout *readoutrelease;

static tbar bars[suppress_freqs+1];

static void compand_change(GtkWidget *w,gpointer in){
  char buffer[80];
  tbar *bar=(tbar *)in;
  float val=multibar_get_value(MULTIBAR(w),0);

  if(val==1.){
    sprintf(buffer,"   off");
  }else 
    sprintf(buffer,"%4.2f",val);

  readout_set(bar->readoutc,buffer);
  
  sset.ratio[bar->number]=1000./val;

}

static void timing_change(GtkWidget *w,gpointer in){
  char buffer[80];
  float smooth=multibar_get_value(MULTIBAR(w),0);
  float trigger=multibar_get_value(MULTIBAR(w),1);
  float release=multibar_get_value(MULTIBAR(w),2);
  
  if(smooth<100){
    sprintf(buffer,"%4.1fms",smooth);
  }else if (smooth<1000){
    sprintf(buffer,"%4.0fms",smooth);
  }else if (smooth<10000){
    sprintf(buffer," %4.2fs",smooth/1000.);
  }else{
    sprintf(buffer," %4.1fs",smooth/1000.);
  }
  readout_set(readoutsmooth,buffer);

  if(trigger<100){
    sprintf(buffer,"%4.1fms",trigger);
  }else if (trigger<1000){
    sprintf(buffer,"%4.0fms",trigger);
  }else if (trigger<10000){
    sprintf(buffer," %4.2fs",trigger/1000.);
  }else{
    sprintf(buffer," %4.1fs",trigger/1000.);
  }
  readout_set(readouttrigger,buffer);

  if(release<100){
    sprintf(buffer,"%4.1fms",release);
  }else if (release<1000){
    sprintf(buffer,"%4.0fms",release);
  }else if (release<10000){
    sprintf(buffer," %4.2fs",release/1000.);
  }else{
    sprintf(buffer," %4.1fs",release/1000.);
  }
  readout_set(readoutrelease,buffer);

  sset.smooth=rint(smooth*10.);
  sset.trigger=rint(trigger*10.);
  sset.release=rint(release*10.);
}

static void suppress_link(GtkToggleButton *b,gpointer in){
  int mode=gtk_toggle_button_get_active(b);
  sset.linkp=mode;
}

void suppresspanel_create(postfish_mainpanel *mp,
			  GtkWidget *windowbutton,
			  GtkWidget *activebutton){
  int i;
  float compand_levels[5]={1,1.5,2,3,5};
  char  *compand_labels[4]={"1.5","2","3","5"};

  float timing_levels[5]={1, 10, 100, 1000, 10000};
  char  *timing_labels[4]={"10ms","     100ms","1s","10s"};
  
  subpanel_generic *panel=subpanel_create(mp,windowbutton,activebutton,
					  &suppress_active,
					  &suppress_visible,
					  "De_verberation filter"," [v] ");
  

  GtkWidget *table=gtk_table_new(suppress_freqs+4,5,0);
  GtkWidget *timinglabel=gtk_label_new("suppressor filter timing");
  GtkWidget *releaselabel=gtk_label_new("release");
  GtkWidget *smoothlabel=gtk_label_new("smooth");
  GtkWidget *triggerlabel=gtk_label_new("trigger");
  GtkWidget *compandlabel=gtk_label_new("suppression depth");

  GtkWidget *linkbutton=
    gtk_check_button_new_with_mnemonic("_link channels into single image");
  GtkWidget *linkbox=gtk_hbox_new(0,0);

  gtk_container_add(GTK_CONTAINER(panel->subpanel_box),table);

  gtk_box_pack_end(GTK_BOX(linkbox),linkbutton,0,0,0);

  gtk_table_attach(GTK_TABLE(table),timinglabel,0,2,0,1,
		   GTK_EXPAND|GTK_FILL,
		   GTK_EXPAND|GTK_FILL,
		   0,5);
  gtk_table_attach(GTK_TABLE(table),smoothlabel,2,3,0,1,
		   GTK_EXPAND|GTK_FILL,
		   GTK_EXPAND|GTK_FILL,
		   0,0);
  gtk_table_attach(GTK_TABLE(table),triggerlabel,3,4,0,1,
		   GTK_EXPAND|GTK_FILL,
		   GTK_EXPAND|GTK_FILL,
		   0,0);
  gtk_table_attach(GTK_TABLE(table),releaselabel,4,5,0,1,
		   GTK_EXPAND|GTK_FILL,
		   GTK_EXPAND|GTK_FILL,
		   0,0);
  gtk_table_attach(GTK_TABLE(table),compandlabel,0,4,2,3,
		   GTK_EXPAND|GTK_FILL,
		   GTK_EXPAND|GTK_FILL,
		   0,5);
  if(input_ch>1)
    gtk_table_attach(GTK_TABLE(table),linkbox,0,5,suppress_freqs+3,
		     suppress_freqs+4,GTK_FILL|GTK_EXPAND,0,0,10);

  gtk_table_set_row_spacing(GTK_TABLE(table),1,5);
  
  gtk_misc_set_alignment(GTK_MISC(timinglabel),0,1.);
  gtk_widget_set_name(timinglabel,"framelabel");
  gtk_misc_set_alignment(GTK_MISC(smoothlabel),.5,1.);
  gtk_widget_set_name(smoothlabel,"scalemarker");
  gtk_misc_set_alignment(GTK_MISC(triggerlabel),.5,1.);
  gtk_widget_set_name(triggerlabel,"scalemarker");
  gtk_misc_set_alignment(GTK_MISC(releaselabel),.5,1.);
  gtk_widget_set_name(releaselabel,"scalemarker");
  gtk_misc_set_alignment(GTK_MISC(compandlabel),0,1.);
  gtk_widget_set_name(compandlabel,"framelabel");

  g_signal_connect (G_OBJECT (linkbutton), "clicked",
		    G_CALLBACK (suppress_link), 0);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(linkbutton),1);

  /* timing controls */
  {
    GtkWidget *slider=multibar_slider_new(4,timing_labels,timing_levels,3);
    
    readoutsmooth=READOUT(readout_new("10.0ms"));
    readouttrigger=READOUT(readout_new("10.0ms"));
    readoutrelease=READOUT(readout_new("10.0ms"));

    multibar_callback(MULTIBAR(slider),timing_change,0);
    
    multibar_thumb_set(MULTIBAR(slider),20,0);
    multibar_thumb_set(MULTIBAR(slider),100,1);
    multibar_thumb_set(MULTIBAR(slider),1000,2);

    gtk_table_attach(GTK_TABLE(table),slider,1,2,1,2,
		     GTK_FILL|GTK_EXPAND,GTK_EXPAND,5,0);
    gtk_table_attach(GTK_TABLE(table),GTK_WIDGET(readoutsmooth),2,3,1,2,
		     0,0,0,0);
    gtk_table_attach(GTK_TABLE(table),GTK_WIDGET(readouttrigger),3,4,1,2,
		     0,0,0,0);
    gtk_table_attach(GTK_TABLE(table),GTK_WIDGET(readoutrelease),4,5,1,2,
		     0,0,0,0);
  }

  /* threshold controls */

  for(i=0;i<suppress_freqs;i++){
    GtkWidget *label=gtk_label_new(suppress_freq_labels[i]);
    gtk_widget_set_name(label,"scalemarker");
    
    bars[i].readoutc=READOUT(readout_new("1.55:1"));
    bars[i].cslider=multibar_slider_new(4,compand_labels,compand_levels,1);
    bars[i].number=i;

    multibar_callback(MULTIBAR(bars[i].cslider),compand_change,bars+i);
    multibar_thumb_set(MULTIBAR(bars[i].cslider),1,0);
    
    gtk_misc_set_alignment(GTK_MISC(label),1,.5);
      
    gtk_table_attach(GTK_TABLE(table),label,0,1,i+3,i+4,
		     GTK_FILL,0,0,0);

    gtk_table_attach(GTK_TABLE(table),bars[i].cslider,1,4,i+3,i+4,
		     GTK_FILL|GTK_EXPAND,GTK_EXPAND,5,0);
    gtk_table_attach(GTK_TABLE(table),GTK_WIDGET(bars[i].readoutc),4,5,
		     i+3,i+4,0,0,0,0);
  }
  subpanel_show_all_but_toplevel(panel);

}

