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
#include "reverb.h"
#include "reverbpanel.h"
#include "config.h"

typedef struct {
  GtkWidget *s;
  GtkWidget *r;
  sig_atomic_t *val;
} slider_readout_pair;

typedef struct{
  subpanel_generic *panel;
  slider_readout_pair *time;
  slider_readout_pair *damp;
  slider_readout_pair *wet;
} reverb_panel_state;

reverb_panel_state *master_panel;
reverb_panel_state **channel_panel;

static void reverbpanel_state_to_config_helper(int bank,plate_set *s,int A){
  config_set_integer("reverb_active",bank,A,0,0,0,s->panel_active);

  config_set_integer("reverb_set",bank,A,0,0,0,s->time);
  config_set_integer("reverb_set",bank,A,0,0,1,s->damping);
  config_set_integer("reverb_set",bank,A,0,0,2,s->wet);
}

void reverbpanel_state_to_config(int bank){
  int i;
  reverbpanel_state_to_config_helper(bank,&plate_master_set,0);
  for(i=0;i<input_ch;i++)
    reverbpanel_state_to_config_helper(bank,plate_channel_set+i,i+1);
}

static void reverbpanel_state_from_config_helper(int bank,plate_set *s,
						 reverb_panel_state *p,int A){

  config_get_sigat("reverb_active",bank,A,0,0,0,&s->panel_active);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->panel->subpanel_activebutton[0]),s->panel_active);


  config_get_sigat("reverb_set",bank,A,0,0,0,&s->time);
    multibar_thumb_set(MULTIBAR(p->time->s),s->time,0);

  config_get_sigat("reverb_set",bank,A,0,0,1,&s->damping);
    multibar_thumb_set(MULTIBAR(p->damp->s),s->damping,0);

  config_get_sigat("reverb_set",bank,A,0,0,2,&s->wet);
    multibar_thumb_set(MULTIBAR(p->wet->s),s->wet,0);

}

void reverbpanel_state_from_config(int bank){
  int i;
  reverbpanel_state_from_config_helper(bank,&plate_master_set,master_panel,0);
  for(i=0;i<input_ch;i++)
    reverbpanel_state_from_config_helper(bank,plate_channel_set+i,channel_panel[i],i+1);
}

static void slider_change(GtkWidget *w,gpointer in){
  char buffer[80];
  slider_readout_pair *p=(slider_readout_pair *)in;
  float val=multibar_get_value(MULTIBAR(p->s),0);
  
  sprintf(buffer,"  %5.2f",val*.01);
  readout_set(READOUT(p->r),buffer);
  
  *p->val=rint(val);
}

static void slider_change_dB(GtkWidget *w,gpointer in){
  char buffer[80];
  slider_readout_pair *p=(slider_readout_pair *)in;
  float val=multibar_get_value(MULTIBAR(p->s),0);
  
  sprintf(buffer,"%+4.1fdB",val*.1);
  readout_set(READOUT(p->r),buffer);
  
  *p->val=rint(val);
}

static reverb_panel_state *reverbpanel_create_helper (postfish_mainpanel *mp,
				       subpanel_generic *panel,
				       plate_set *ps){
  
  char *labels[11]={"","1","2","3","4","5","6","7","8","9","  10"};
  float levels[11]={0,100,200,300,400,500,600,700,800,900,1000};

  char *labelsdB[11]={"","-40","-20","-10","-3","0","+3","+10","+20","+40","  +80"};
  float levelsdB[11]={-800,-400,-200,-100,-30,0,30,100,200,400,800};

  GtkWidget *table=gtk_table_new(3,3,0);
  slider_readout_pair *p1=malloc(sizeof(*p1));
  slider_readout_pair *p2=malloc(sizeof(*p2));
  slider_readout_pair *p3=malloc(sizeof(*p3));

  GtkWidget *l1=gtk_label_new("Plate size ");
  GtkWidget *l2=gtk_label_new("Damping ");
  GtkWidget *l3=gtk_label_new("Wet level ");

  reverb_panel_state *p=calloc(1,sizeof(*p));
  p->panel=panel;

  gtk_misc_set_alignment(GTK_MISC(l1),1,.5);
  gtk_misc_set_alignment(GTK_MISC(l2),1,.5);
  gtk_misc_set_alignment(GTK_MISC(l3),1,.5);

  p->time=p1;
  p1->s=multibar_slider_new(11,labels,levels,1);
  p1->r=readout_new("10.00");
  p1->val=&ps->time;

  p->damp=p2;
  p2->s=multibar_slider_new(11,labels,levels,1);
  p2->r=readout_new("10.00");
  p2->val=&ps->damping;

  p->wet=p3;
  p3->s=multibar_slider_new(11,labelsdB,levelsdB,1);
  p3->r=readout_new("10.00");
  p3->val=&ps->wet;

  gtk_table_attach(GTK_TABLE(table),l1,0,1,0,1,
		   GTK_FILL,GTK_FILL|GTK_EXPAND,2,0);
  gtk_table_attach(GTK_TABLE(table),p1->s,1,2,0,1,
		   GTK_FILL,GTK_FILL|GTK_EXPAND,2,0);
  gtk_table_attach(GTK_TABLE(table),p1->r,2,3,0,1,
		   GTK_FILL,GTK_FILL|GTK_EXPAND,2,0);

  gtk_table_attach(GTK_TABLE(table),l2,0,1,1,2,
		   GTK_FILL,GTK_FILL|GTK_EXPAND,2,0);
  gtk_table_attach(GTK_TABLE(table),p2->s,1,2,1,2,
		   GTK_FILL,GTK_FILL|GTK_EXPAND,2,0);
  gtk_table_attach(GTK_TABLE(table),p2->r,2,3,1,2,
		   GTK_FILL,GTK_FILL|GTK_EXPAND,2,0);

  gtk_table_attach(GTK_TABLE(table),l3,0,1,2,3,
		   GTK_FILL,GTK_FILL|GTK_EXPAND,2,0);
  gtk_table_attach(GTK_TABLE(table),p3->s,1,2,2,3,
		   GTK_FILL,GTK_FILL|GTK_EXPAND,2,0);
  gtk_table_attach(GTK_TABLE(table),p3->r,2,3,2,3,
		   GTK_FILL,GTK_FILL|GTK_EXPAND,2,0);

  gtk_container_add(GTK_CONTAINER(panel->subpanel_box),table);

  multibar_callback(MULTIBAR(p1->s),slider_change,p1);
  multibar_callback(MULTIBAR(p2->s),slider_change,p2);
  multibar_callback(MULTIBAR(p3->s),slider_change_dB,p3);

  multibar_thumb_set(MULTIBAR(p1->s),200,0);
  multibar_thumb_set(MULTIBAR(p2->s),200,0);
  multibar_thumb_set(MULTIBAR(p3->s),0,0);

  multibar_thumb_increment(MULTIBAR(p1->s),1,10);
  multibar_thumb_increment(MULTIBAR(p2->s),1,10);
  multibar_thumb_increment(MULTIBAR(p3->s),1,10);
  
  subpanel_show_all_but_toplevel(panel);
  return p;
}

void reverbpanel_create_master(postfish_mainpanel *mp,
                               GtkWidget *windowbutton,
                               GtkWidget *activebutton){
  
  char *shortcut[]={" r "};
  subpanel_generic *panel=subpanel_create(mp,windowbutton,&activebutton,
					  &plate_master_set.panel_active,
					  &plate_master_set.panel_visible,
                                          "_Plate Reverb (master)",shortcut,
                                          0,1);
  master_panel=reverbpanel_create_helper(mp,panel,&plate_master_set);
}

void reverbpanel_create_channel(postfish_mainpanel *mp,
                                GtkWidget **windowbutton,
                                GtkWidget **activebutton){
  int i;
  /* a panel for each channel */
  channel_panel=calloc(input_ch,sizeof(*channel_panel));

  for(i=0;i<input_ch;i++){
    subpanel_generic *panel;
    char buffer[80];
    
    sprintf(buffer,"_Plate Reverb (channel %d)",i+1);
    panel=subpanel_create(mp,windowbutton[i],activebutton+i,
                          &plate_channel_set[i].panel_active,
                          &plate_channel_set[i].panel_visible,
                          buffer,NULL,
                          i,1);
    
    channel_panel[i]=reverbpanel_create_helper(mp,panel,plate_channel_set+i);
  }
}


