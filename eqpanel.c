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
#include "freq.h"
#include "eq.h"

extern int input_ch;
extern int input_size;
extern int input_rate;

extern eq_settings eq_master_set;
extern eq_settings *eq_channel_set;

typedef struct {
  GtkWidget *slider;
  GtkWidget *readout;
  eq_settings *s;
  int number;
} bar;

static bar *m_bars;
static bar **c_bars;

static void slider_change(GtkWidget *w,gpointer in){
  char buffer[80];
  bar *b=(bar *)in;
  float val=multibar_get_value(MULTIBAR(b->slider),0);
  
  sprintf(buffer,"%+3.0fdB",val);
  readout_set(READOUT(b->readout),buffer);
  
  eq_set(b->s,b->number,val);

}

static bar *eqpanel_create_helper(postfish_mainpanel *mp,
			   subpanel_generic *panel,
			   eq_settings *es){

  int i;
  char *labels[16]={"","110","100","90","80","70","60","50","40",
		    "30","20","10","0","+10","+20","+30"};
  float levels[16]={-120,-110,-100,-90,-80,-70,-60,-50,-40,
		     -30,-20,-10,0,10,20,30};

  GtkWidget *slidertable=gtk_table_new(eq_freqs,3,0);
  bar *bars=calloc(eq_freqs,sizeof(*bars));
  
  for(i=0;i<eq_freqs;i++){
    const char *labeltext=eq_freq_labels[i];
    
    GtkWidget *label=gtk_label_new(labeltext);
    gtk_widget_set_name(label,"smallmarker");

    bars[i].readout=readout_new("+00dB");
    bars[i].slider=multibar_new(16,labels,levels,1,
				LO_DECAY|HI_DECAY|LO_ATTACK|HI_ATTACK);
    bars[i].number=i;
    bars[i].s=es;

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

  gtk_box_pack_start(GTK_BOX(panel->subpanel_box),slidertable,1,1,4);
  subpanel_show_all_but_toplevel(panel);

  return bars;
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
  
  m_bars=eqpanel_create_helper(mp,panel,&eq_master_set);
}

void eqpanel_create_channel(postfish_mainpanel *mp,
				 GtkWidget **windowbutton,
				 GtkWidget **activebutton){
  int i;
  c_bars=malloc(input_ch*sizeof(*c_bars));

  /* a panel for each channel */
  for(i=0;i<input_ch;i++){
    subpanel_generic *panel;
    char buffer[80];

    sprintf(buffer,"_Equalizer (channel %d)",i+1);

    panel=subpanel_create(mp,windowbutton[i],activebutton+i,
			  &eq_channel_set[i].panel_active,
			  &eq_channel_set[i].panel_visible,
			  buffer,0,i,1);
  
    c_bars[i]=eqpanel_create_helper(mp,panel,eq_channel_set+i);
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
      multibar_set(MULTIBAR(m_bars[i].slider),rmsfeed[i],peakfeed[i],
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
	
	multibar_set(MULTIBAR(c_bars[j][i].slider),rms,peak,
		     input_ch,(displayit && eq_channel_set[j].panel_visible));
      }
    }
  }
}

void eqpanel_reset(void){
  int i,j;
  for(i=0;i<eq_freqs;i++)
    multibar_reset(MULTIBAR(m_bars[i].slider));
  
  for(i=0;i<eq_freqs;i++)
    for(j=0;j<input_ch;j++)
      multibar_reset(MULTIBAR(c_bars[j][i].slider));
}

