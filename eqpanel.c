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
extern int input_ch;
extern int input_size;
extern int input_rate;

typedef struct {
  GtkWidget *slider;
  GtkWidget *readout;
  int number;
} bar;

bar bars[freqs];

static void slider_change(GtkWidget *w,gpointer in){
  char buffer[80];
  bar *b=(bar *)in;
  gdouble val=multibar_get_value(MULTIBAR(b->slider),0);
  
  sprintf(buffer,"%+5.1f dB",val);
  readout_set(READOUT(b->readout),buffer);
  
  //eq_setlevel(val,p->number);

}

void eqpanel_create(postfish_mainpanel *mp,
		    GtkWidget *windowbutton,
		    GtkWidget *activebutton){
  int i;
  char *labels[15]={"-110","-100","-90","-80","-70","-60","-50","-40",
		    "-30","-20","-10","0","+10","+20","+30"};
  double levels[16]={-120,-110,-100,-90,-80,-70,-60,-50,-40,
		     -30,-20,-10,0,10,20,30};

  subpanel_generic *panel=subpanel_create(mp,windowbutton,activebutton,
					  &eq_active,
					  "_Equalization filter"," [e] ");
  
  GtkWidget *slidertable=gtk_table_new(freqs,3,0);

  for(i=0;i<freqs;i++){
    const char *labeltext="";
    if((i&1)==0)labeltext=freq_frequency_label(i);

    GtkWidget *label=gtk_label_new(labeltext);
    bars[i].readout=readout_new("+00.0 dB");
    bars[i].slider=multibar_new(15,labels,levels,1,
				LO_ATTACK|LO_DECAY|HI_DECAY);
    bars[i].number=i;

    multibar_callback(MULTIBAR(bars[i].slider),slider_change,bars+i);
    multibar_thumb_set(MULTIBAR(bars[i].slider),0.,0);
    multibar_thumb_bounds(MULTIBAR(bars[i].slider),-60,30);

    gtk_misc_set_alignment(GTK_MISC(label),1,.5);

    gtk_table_attach(GTK_TABLE(slidertable),label,0,1,i,i+1,
		     GTK_FILL,0,10,0);
    gtk_table_attach(GTK_TABLE(slidertable),bars[i].readout,1,2,i,i+1,
		     GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(slidertable),bars[i].slider,2,3,i,i+1,
		     GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,0,0);
  }

  gtk_box_pack_start(GTK_BOX(panel->subpanel_box),slidertable,1,1,4);

}

void eqpanel_feedback(void){
#if 0
  int clip[input_ch],count;
  double peak[input_ch];
  if(pull_declip_feedback(clip,peak,&count)){
    int i;
    for(i=0;i<input_ch;i++){
      double val[2],zero[2];
      val[0]=-1.,zero[0]=-1.;
      val[1]=(count?clip[i]*100./count-.1:-1);
      zero[1]=-1.;
      multibar_set(MULTIBAR(feedback_bars[i]),zero,val,2);
      val[1]=(count?peak[i]:-1);
      multibar_set(MULTIBAR(trigger_bars[i]),zero,val,2);
      if(clip[i]){
	multibar_setwarn(MULTIBAR(mainpanel_inbar));
	multibar_setwarn(MULTIBAR(feedback_bars[i]));
	multibar_setwarn(MULTIBAR(trigger_bars[i]));
      }
    }
  }
#endif
}

void eqpanel_reset(void){
  int i;
  for(i=0;i<freqs;i++)
    multibar_reset(MULTIBAR(bars[i].slider));
}
