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

#include "postfish.h"
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "readout.h"
#include "multibar.h"
#include "mainpanel.h"
#include "subpanel.h"
#include "feedback.h"
#include "freq.h"
#include "eq.h"
#include "config.h"

typedef struct {
  GtkWidget *slider;
  GtkWidget *readout;
  struct eps *p;
  int number;
} bar;

typedef struct eps {
  subpanel_generic *panel;
  bar *bars;
  eq_settings *s;
  int av_callback_enter;
} eq_panel_state;

static eq_panel_state *master_panel;
static eq_panel_state **channel_panel;

static void eqpanel_state_to_config_helper(int bank,eq_settings *s,int A){
  config_set_integer("eq_active",bank,A,0,0,0,s->panel_active);
  config_set_vector("eq_settings",bank,A,0,0,eq_freqs,s->settings);
}

void eqpanel_state_to_config(int bank){
  int i;
  eqpanel_state_to_config_helper(bank,&eq_master_set,0);
  for(i=0;i<input_ch;i++)
    eqpanel_state_to_config_helper(bank,eq_channel_set+i,i+1);
}

static void eqpanel_state_from_config_helper(int bank,eq_settings *s,
						 eq_panel_state *p,int A){

  int i;
  config_get_sigat("eq_active",bank,A,0,0,0,&s->panel_active);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->panel->subpanel_activebutton[0]),s->panel_active);

  config_get_vector("eq_settings",bank,A,0,0,eq_freqs,s->settings);
  for(i=0;i<eq_freqs;i++)
    multibar_thumb_set(MULTIBAR(p->bars[i].slider),s->settings[i]*.1,0);

}

void eqpanel_state_from_config(int bank){
  int i;
  eqpanel_state_from_config_helper(bank,&eq_master_set,master_panel,0);
  for(i=0;i<input_ch;i++)
    eqpanel_state_from_config_helper(bank,eq_channel_set+i,channel_panel[i],i+1);
}

static float determine_average(eq_panel_state *p){
  bar *b=p->bars;
  int i;
  float acc=0;
  for(i=0;i<eq_freqs;i++)
    acc+=multibar_get_value(MULTIBAR(b[i].slider),0);
  return acc/eq_freqs;
}

static void average_change(GtkWidget *w,gpointer in){
  eq_panel_state *p=(eq_panel_state *)in;
  if(!p->av_callback_enter){
    
    float av=multibar_get_value(MULTIBAR(p->bars[eq_freqs].slider),0);
    float actual=determine_average(p);
    int i;

    p->av_callback_enter=1;
    for(i=0;i<eq_freqs;i++){
      float val=multibar_get_value(MULTIBAR(p->bars[i].slider),0) + av - actual;
      multibar_thumb_set(MULTIBAR(p->bars[i].slider),val,0);
    }
    
    p->av_callback_enter=0;
  }
}

static void slider_change(GtkWidget *w,gpointer in){
  char buffer[80];
  bar *b=(bar *)in;
  float val=multibar_get_value(MULTIBAR(b->slider),0);
  
  sprintf(buffer,"%+3.0fdB",val);
  readout_set(READOUT(b->readout),buffer);
  
  eq_set(b->p->s,b->number,val);

  if(!b->p->av_callback_enter){
    b->p->av_callback_enter=1;
    float actual=determine_average(b->p);
    multibar_thumb_set(MULTIBAR(b->p->bars[eq_freqs].slider),actual,0);
    b->p->av_callback_enter=0;
  }
}

static eq_panel_state *eqpanel_create_helper(postfish_mainpanel *mp,
			   subpanel_generic *panel,
			   eq_settings *es){

  int i;
  char *labels[16]={"","110","100","90","80","70","60","50","40",
		    "30","20","10","0","+10","+20","+30"};
  char *labels2[16]={"","","","","","","60","50","40",
		    "30","20","10","0","+10","+20","+30"};
  float levels[16]={-120,-110,-100,-90,-80,-70,-60,-50,-40,
		     -30,-20,-10,0,10,20,30};

  GtkWidget *slidertable=gtk_table_new(eq_freqs+1,3,0);
  bar *bars=calloc(eq_freqs+1,sizeof(*bars));
  eq_panel_state *p=calloc(1,sizeof(*p));

  p->bars=bars;
  p->s=es;
  p->panel=panel;
  p->av_callback_enter=1;

  for(i=0;i<eq_freqs;i++){
    const char *labeltext=eq_freq_labels[i];
    
    GtkWidget *label=gtk_label_new(labeltext);
    gtk_widget_set_name(label,"smallmarker");

    bars[i].readout=readout_new("+00dB");
    bars[i].slider=multibar_new(16,labels,levels,1,
				LO_DECAY|HI_DECAY|LO_ATTACK|HI_ATTACK);
    bars[i].number=i;
    bars[i].p=p;

    multibar_callback(MULTIBAR(bars[i].slider),slider_change,bars+i);
    multibar_thumb_set(MULTIBAR(bars[i].slider),0.,0);
    multibar_thumb_bounds(MULTIBAR(bars[i].slider),-60,30);
    multibar_thumb_increment(MULTIBAR(bars[i].slider),1,10);

    gtk_misc_set_alignment(GTK_MISC(label),1,.5);

    gtk_table_attach(GTK_TABLE(slidertable),label,0,1,i,i+1,
		     GTK_FILL,0,10,0);
    gtk_table_attach(GTK_TABLE(slidertable),bars[i].readout,2,3,i,i+1,
		     GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(slidertable),bars[i].slider,1,2,i,i+1,
		     GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,0,0);
  }

  {
    GtkWidget *label=gtk_label_new("average");
    gtk_widget_set_name(label,"smallmarker");

    bars[i].slider=multibar_slider_new(16,labels2,levels,1);
    bars[i].number=i;
    bars[i].p=p;

    multibar_callback(MULTIBAR(bars[i].slider),average_change,p);
    multibar_thumb_bounds(MULTIBAR(bars[i].slider),-60,30);
    multibar_thumb_increment(MULTIBAR(bars[i].slider),1,10);

    gtk_misc_set_alignment(GTK_MISC(label),1,.5);

    gtk_table_attach(GTK_TABLE(slidertable),label,0,1,i,i+1,
		     GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(slidertable),bars[i].slider,1,2,i,i+1,
		     GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,0,0);

  }

  gtk_box_pack_start(GTK_BOX(panel->subpanel_box),slidertable,1,1,4);
  subpanel_show_all_but_toplevel(panel);
  p->av_callback_enter=0;

  return p;
}

void eqpanel_create_master(postfish_mainpanel *mp,
                                GtkWidget *windowbutton,
                                GtkWidget *activebutton){

  char *shortcut[]={" e "};
  subpanel_generic *panel=subpanel_create(mp,windowbutton,&activebutton,
					  &eq_master_set.panel_active,
					  &eq_master_set.panel_visible,
					  "_Equalizer (master)",shortcut,
					  0,1);
  
  master_panel=eqpanel_create_helper(mp,panel,&eq_master_set);
}

void eqpanel_create_channel(postfish_mainpanel *mp,
				 GtkWidget **windowbutton,
				 GtkWidget **activebutton){
  int i;
  channel_panel=malloc(input_ch*sizeof(*channel_panel));

  /* a panel for each channel */
  for(i=0;i<input_ch;i++){
    subpanel_generic *panel;
    char buffer[80];

    sprintf(buffer,"_Equalizer (channel %d)",i+1);

    panel=subpanel_create(mp,windowbutton[i],activebutton+i,
			  &eq_channel_set[i].panel_active,
			  &eq_channel_set[i].panel_visible,
			  buffer,0,i,1);
  
    channel_panel[i]=eqpanel_create_helper(mp,panel,eq_channel_set+i);
  }
}


static float **peakfeed=0;
static float **rmsfeed=0;

void eqpanel_feedback(int displayit){
  int i,j;
  if(!peakfeed){
    peakfeed=malloc(sizeof(*peakfeed)*eq_freqs);
    rmsfeed=malloc(sizeof(*rmsfeed)*eq_freqs);

    for(i=0;i<eq_freqs;i++){
      peakfeed[i]=malloc(sizeof(**peakfeed)*max(input_ch,OUTPUT_CHANNELS));
      rmsfeed[i]=malloc(sizeof(**rmsfeed)*max(input_ch,OUTPUT_CHANNELS));
    }
  }
  
  if(pull_eq_feedback_master(peakfeed,rmsfeed)==1)
    for(i=0;i<eq_freqs;i++)
      multibar_set(MULTIBAR(master_panel->bars[i].slider),rmsfeed[i],peakfeed[i],
		   OUTPUT_CHANNELS,(displayit && eq_master_set.panel_visible));
  

  if(pull_eq_feedback_channel(peakfeed,rmsfeed)==1){
    for(j=0;j<input_ch;j++){
      for(i=0;i<eq_freqs;i++){
	float rms[input_ch];
	float peak[input_ch];
	
	memset(rms,0,sizeof(rms));
        memset(peak,0,sizeof(peak));
        rms[j]=rmsfeed[i][j];
        peak[j]=peakfeed[i][j];
	
	multibar_set(MULTIBAR(channel_panel[j]->bars[i].slider),rms,peak,
		     input_ch,(displayit && eq_channel_set[j].panel_visible));
      }
    }
  }
}

void eqpanel_reset(void){
  int i,j;
  for(i=0;i<eq_freqs;i++)
    multibar_reset(MULTIBAR(master_panel->bars[i].slider));
  
  for(i=0;i<eq_freqs;i++)
    for(j=0;j<input_ch;j++)
      multibar_reset(MULTIBAR(channel_panel[j]->bars[i].slider));
}

