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

extern sig_atomic_t eq_active;
extern sig_atomic_t eq_visible;
extern int input_ch;
extern int input_size;
extern int input_rate;

typedef struct {
  GtkWidget *slider;
  GtkWidget *readout;
  int number;
} bar;

static bar bars[eq_freqs];

static void slider_change(GtkWidget *w,gpointer in){
  char buffer[80];
  bar *b=(bar *)in;
  float val=multibar_get_value(MULTIBAR(b->slider),0);
  
  sprintf(buffer,"%+3.0fdB",val);
  readout_set(READOUT(b->readout),buffer);
  
  eq_set(b->number,val);

}

void eqpanel_create(postfish_mainpanel *mp,
		    GtkWidget *windowbutton,
		    GtkWidget *activebutton){
  int i;
  char *labels[15]={"110","100","90","80","70","60","50","40",
		    "30","20","10","0","+10","+20","+30"};
  float levels[16]={-120,-110,-100,-90,-80,-70,-60,-50,-40,
		     -30,-20,-10,0,10,20,30};
  char *shortcut[]={" e "};

  subpanel_generic *panel=subpanel_create(mp,windowbutton,&activebutton,
					  &eq_active,
					  &eq_visible,
					  "_Equalization filter",shortcut,
					  0,1);
  
  GtkWidget *slidertable=gtk_table_new(eq_freqs,3,0);

  for(i=0;i<eq_freqs;i++){
    const char *labeltext=eq_freq_labels[i];

    GtkWidget *label=gtk_label_new(labeltext);
    gtk_widget_set_name(label,"smallmarker");

    bars[i].readout=readout_new("+00dB");
    bars[i].slider=multibar_new(15,labels,levels,1,
				LO_DECAY|HI_DECAY|LO_ATTACK|HI_ATTACK);
    bars[i].number=i;

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

}

static float **peakfeed=0;
static float **rmsfeed=0;

void eqpanel_feedback(int displayit){
  int i;
  if(!peakfeed){
    peakfeed=malloc(sizeof(*peakfeed)*eq_freqs);
    rmsfeed=malloc(sizeof(*rmsfeed)*eq_freqs);

    for(i=0;i<eq_freqs;i++){
      peakfeed[i]=malloc(sizeof(**peakfeed)*input_ch);
      rmsfeed[i]=malloc(sizeof(**rmsfeed)*input_ch);
    }
  }
  
  if(pull_eq_feedback(peakfeed,rmsfeed)==1)
    for(i=0;i<eq_freqs;i++)
      multibar_set(MULTIBAR(bars[i].slider),rmsfeed[i],peakfeed[i],
		   input_ch,(displayit && eq_visible));
  
}

void eqpanel_reset(void){
  int i;
  for(i=0;i<eq_freqs;i++)
    multibar_reset(MULTIBAR(bars[i].slider));
}

