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
#include "limit.h"
#include "limitpanel.h"
#include "config.h"

static GtkWidget *active;
static GtkWidget *t_slider;
static GtkWidget *k_slider;
static GtkWidget *d_slider;
static GtkWidget *a_slider;

void limitpanel_state_to_config(int bank){
  config_set_integer("limit_active",bank,0,0,0,0,limit_active);
  config_set_integer("limit_set",bank,0,0,0,0,limitset.thresh);
  config_set_integer("limit_set",bank,0,0,0,1,limitset.depth);
  config_set_integer("limit_set",bank,0,0,0,2,limitset.decay);
}

void limitpanel_state_from_config(int bank){
  config_get_sigat("limit_active",bank,0,0,0,0,&limit_active);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(active),limit_active);

  config_get_sigat("limit_set",bank,0,0,0,0,&limitset.thresh);
  multibar_thumb_set(MULTIBAR(t_slider),limitset.thresh*.1,0);

  config_get_sigat("limit_set",bank,0,0,0,1,&limitset.depth);
  multibar_thumb_set(MULTIBAR(k_slider),limitset.depth*.1,0);

  config_get_sigat("limit_set",bank,0,0,0,2,&limitset.decay);
  multibar_thumb_set(MULTIBAR(d_slider),limitset.decay*.1,0);
}

static void limit_change(GtkWidget *w,gpointer in){
  char buffer[80];
  Readout *r=(Readout *)in;
  float val=multibar_get_value(MULTIBAR(w),0);

  sprintf(buffer,"%+5.1fdB",val);
  readout_set(r,buffer);
  
  limitset.thresh=rint(val*10.);
}

static void depth_change(GtkWidget *w,gpointer in){
  char buffer[80];
  Readout *r=(Readout *)in;
  float val=multibar_get_value(MULTIBAR(w),0);

  sprintf(buffer,"%+5.1fdB",val);
  readout_set(r,buffer);
  
  limitset.depth=rint(val*10.);
}

static void decay_change(GtkWidget *w,gpointer in){
  char buffer[80];
  Readout *r=(Readout *)in;
  float v=multibar_get_value(MULTIBAR(w),0);


  if(v<10){
    sprintf(buffer," %4.2fms",v);
  }else if(v<100){
    sprintf(buffer," %4.1fms",v);
  }else if (v<1000){
    sprintf(buffer," %4.0fms",v);
  }else if (v<10000){
    sprintf(buffer," %4.2fs",v/1000.);
  }else{
    sprintf(buffer," %4.1fs",v/1000.);
  }

  readout_set(READOUT(r),buffer);

  limitset.decay=rint(v*10.);
}

void limitpanel_create(postfish_mainpanel *mp,
		       GtkWidget *windowbutton,
		       GtkWidget *activebutton){


  char *labels[9]={"","-80","-60","-40","-20","-10","-6","-3","+0"};
  float levels[9]={-80,-60,-50,-40,-30,-10,-6,-3,0};

  char *labels2[5]={"","-20","-10","-3","0"};
  float levels2[5]={-30,-20,-10,-3,0};
  
  char *rlabels[4]={"","6","   20","40"};
  float rlevels[4]={0,3,10,20};

  float timing_levels[6]={.1,1,10,100,1000,10000};
  char  *timing_labels[6]={"","1ms","10ms","100ms","1s","10s"};

  char *shortcut[]={" l "};

  subpanel_generic *panel=subpanel_create(mp,windowbutton,&activebutton,
					  &limit_active,
					  &limit_visible,
					  "Hard _Limiter",shortcut,
					  0,1);
  
  GtkWidget *slidertable=gtk_table_new(4,4,0);

  GtkWidget *label1=gtk_label_new("knee bend");
  GtkWidget *label2=gtk_label_new("knee depth");
  GtkWidget *label3=gtk_label_new("decay speed");
  GtkWidget *label4=gtk_label_new("attenuation");

  GtkWidget *readout1=readout_new("+XXXXdB");
  GtkWidget *readout2=readout_new("+XXXXdB");
  GtkWidget *readout3=readout_new("+XXXXms");

  GtkWidget *slider2=multibar_slider_new(5,labels2,levels2,1);
  GtkWidget *slider3=multibar_slider_new(6,timing_labels,timing_levels,1);

  active=activebutton;

  t_slider=multibar_new(9,labels,levels,1,HI_DECAY);
  a_slider=multibar_new(4,rlabels,rlevels,0,0);

  k_slider=slider2;
  d_slider=slider3;

  gtk_misc_set_alignment(GTK_MISC(label1),1,.5);
  gtk_misc_set_alignment(GTK_MISC(label2),1,.5);
  gtk_misc_set_alignment(GTK_MISC(label3),1,.5);
  gtk_widget_set_name(label4,"scalemarker");

  gtk_table_attach(GTK_TABLE(slidertable),label1,0,1,1,2,GTK_FILL,GTK_FILL|GTK_EXPAND,2,0);
  gtk_table_attach(GTK_TABLE(slidertable),label2,0,1,2,3,GTK_FILL,GTK_FILL|GTK_EXPAND,2,0);
  gtk_table_attach(GTK_TABLE(slidertable),label3,0,1,3,4,GTK_FILL,GTK_FILL|GTK_EXPAND,2,0);
  gtk_table_attach(GTK_TABLE(slidertable),label4,3,4,0,1,GTK_FILL,GTK_FILL|GTK_EXPAND,0,0);
 
  gtk_table_attach(GTK_TABLE(slidertable),t_slider,1,2,1,2,GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,0,0);
  gtk_table_attach(GTK_TABLE(slidertable),a_slider,3,4,1,2,GTK_FILL,GTK_FILL|GTK_EXPAND,0,0);
  gtk_table_attach(GTK_TABLE(slidertable),slider2,1,2,2,3,GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,0,4);
  gtk_table_attach(GTK_TABLE(slidertable),slider3,1,2,3,4,GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,0,0);

  gtk_table_attach(GTK_TABLE(slidertable),readout1,2,3,1,2,GTK_FILL,GTK_FILL|GTK_EXPAND,0,0);
  gtk_table_attach(GTK_TABLE(slidertable),readout2,2,3,2,3,GTK_FILL,GTK_FILL|GTK_EXPAND,0,4);
  gtk_table_attach(GTK_TABLE(slidertable),readout3,2,3,3,4,GTK_FILL,GTK_FILL|GTK_EXPAND,0,0);

  gtk_container_add(GTK_CONTAINER(panel->subpanel_box),slidertable);

  multibar_callback(MULTIBAR(t_slider),limit_change,readout1);
  multibar_callback(MULTIBAR(slider2),depth_change,readout2);
  multibar_callback(MULTIBAR(slider3),decay_change,readout3);

  multibar_thumb_set(MULTIBAR(t_slider),0.,0);
  multibar_thumb_set(MULTIBAR(slider2),0.,0);
  multibar_thumb_set(MULTIBAR(slider3),10.,0);
  
  subpanel_show_all_but_toplevel(panel);

}

static float *peakfeed=0;
static float *attfeed=0;
static float *zerofeed=0;

void limitpanel_feedback(int displayit){
  if(!peakfeed){
    int i;
    peakfeed=malloc(sizeof(*peakfeed)*OUTPUT_CHANNELS);
    attfeed=malloc(sizeof(*attfeed)*OUTPUT_CHANNELS);
    zerofeed=malloc(sizeof(*zerofeed)*OUTPUT_CHANNELS);
    for(i=0;i<OUTPUT_CHANNELS;i++)zerofeed[i]=-150.;
  }
  
  if(pull_limit_feedback(peakfeed,attfeed)==1){
    multibar_set(MULTIBAR(t_slider),zerofeed,peakfeed,
		 OUTPUT_CHANNELS,(displayit && limit_visible));
    multibar_set(MULTIBAR(a_slider),zerofeed,attfeed,
		 OUTPUT_CHANNELS,(displayit && limit_visible));
  }
}

void limitpanel_reset(void){
  multibar_reset(MULTIBAR(t_slider));
  multibar_reset(MULTIBAR(a_slider));
}

