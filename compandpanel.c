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
#include "compand.h"
#include "compandpanel.h"

extern sig_atomic_t compand_active;
extern sig_atomic_t compand_visible;
extern int input_ch;
extern int input_size;
extern int input_rate;

typedef struct {
  GtkWidget *slider;
  GtkWidget *readoutg;
  GtkWidget *readoute;
  GtkWidget *readoutc;
  int number;
} cbar;

static cbar bars[freqs];

static void slider_change(GtkWidget *w,gpointer in){
  char buffer[80];
  cbar *b=(cbar *)in;

  gdouble val=multibar_get_value(MULTIBAR(b->slider),0);
  sprintf(buffer,"%+4.0f dB",val);
  readout_set(READOUT(b->readoutg),buffer);
  compand_g_set(b->number,val);
  
  val=multibar_get_value(MULTIBAR(b->slider),1);
  sprintf(buffer,"%+4.0f dB",val);
  readout_set(READOUT(b->readoute),buffer);
  compand_e_set(b->number,val);

  val=multibar_get_value(MULTIBAR(b->slider),2);
  sprintf(buffer,"%+4.0f dB",val);
  readout_set(READOUT(b->readoutc),buffer);
  compand_c_set(b->number,val);

}

void compandpanel_create(postfish_mainpanel *mp,
			 GtkWidget *windowbutton,
			 GtkWidget *activebutton){
  int i;
  char *labels[14]={"-130","-120","-110","-100","-90","-80","-70",
		    "-60","-50","-40","-30","-20","-10","0"};
  double levels[15]={-140,-130,-120,-110,-100,-90,-80,-70,-60,-50,-40,
		     -30,-20,-10,0};

  subpanel_generic *panel=subpanel_create(mp,windowbutton,activebutton,
					  &compand_active,
					  &compand_visible,
					  "_Compander and Noise Gate"," [c] ");
  
  GtkWidget *slidertable=gtk_table_new(freqs,5,0);

  for(i=0;i<freqs;i++){
    const char *labeltext=freq_frequency_label(i);

    GtkWidget *label=gtk_label_new(labeltext);
    gtk_widget_set_name(label,"smallmarker");

    bars[i].readoutg=readout_new("  +0 dB");
    bars[i].readoute=readout_new("  +0 dB");
    bars[i].readoutc=readout_new("  +0 dB");
    bars[i].slider=multibar_new(14,labels,levels,3,
				LO_DECAY|HI_DECAY|LO_ATTACK|HI_ATTACK);
    bars[i].number=i;

    multibar_callback(MULTIBAR(bars[i].slider),slider_change,bars+i);
    multibar_thumb_set(MULTIBAR(bars[i].slider),-140.,0);
    multibar_thumb_set(MULTIBAR(bars[i].slider),-140.,1);
    multibar_thumb_set(MULTIBAR(bars[i].slider),0.,2);
    multibar_thumb_bounds(MULTIBAR(bars[i].slider),-140,0);
    multibar_thumb_increment(MULTIBAR(bars[i].slider),1.,10.);

    gtk_misc_set_alignment(GTK_MISC(label),1,.5);

    gtk_table_attach(GTK_TABLE(slidertable),label,0,1,i,i+1,
		     GTK_FILL,0,10,0);
    gtk_table_attach(GTK_TABLE(slidertable),bars[i].readoutg,2,3,i,i+1,
		     GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(slidertable),bars[i].readoute,3,4,i,i+1,
		     GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(slidertable),bars[i].readoutc,4,5,i,i+1,
		     GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(slidertable),bars[i].slider,1,2,i,i+1,
		     GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,0,0);
  }

  gtk_box_pack_start(GTK_BOX(panel->subpanel_box),slidertable,1,1,4);

}

static double **peakfeed=0;
static double **rmsfeed=0;

void compandpanel_feedback(int displayit){
  int i;
  if(!peakfeed){
    peakfeed=malloc(sizeof(*peakfeed)*freqs);
    rmsfeed=malloc(sizeof(*rmsfeed)*freqs);

    for(i=0;i<freqs;i++){
      peakfeed[i]=malloc(sizeof(**peakfeed)*input_ch);
      rmsfeed[i]=malloc(sizeof(**rmsfeed)*input_ch);
    }
  }
  
  if(pull_compand_feedback(peakfeed,rmsfeed)==1)
    if(displayit && compand_visible)
      for(i=0;i<freqs;i++)
	multibar_set(MULTIBAR(bars[i].slider),rmsfeed[i],peakfeed[i],input_ch);
}

void compandpanel_reset(void){
  int i;
  for(i=0;i<freqs;i++)
    multibar_reset(MULTIBAR(bars[i].slider));
}

