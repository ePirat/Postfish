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

static void link_toggled(GtkToggleButton *b,gpointer in){
  int active=gtk_toggle_button_get_active(b);
  compand_set_link(active);    
}

static void suppressor_mode(GtkButton *b,gpointer in){
  int mode=(int)in;

  if(mode)
    /* peak mode */
    compand_s_peakmode();
  else
    /* rms mode */
    compand_s_rmsmode();

}

static void suppress_bias_change(GtkWidget *w,gpointer in){
  char buffer[80];
  Readout *r=READOUT(in);
  gdouble val=multibar_get_value(MULTIBAR(w),0);

  sprintf(buffer,"%+2.0fdB",val);
  readout_set(r,buffer);
  compand_s_setbias(val);
}

static void suppress_decay_change(GtkWidget *w,gpointer in){
  char buffer[80];
  Readout *r=READOUT(in);
  gdouble val=-multibar_get_value(MULTIBAR(w),0);

  sprintf(buffer,"%.2f",val);

  if(val==1.)
    sprintf(buffer,"fast");

  if(val==.01)
    sprintf(buffer,"slow");

  readout_set(r,buffer);
  compand_s_setdecay(val);
}

static void suppress_ratio_change(GtkWidget *w,gpointer in){
  char buffer[80];
  Readout *r=READOUT(in);
  gdouble val=-multibar_get_value(MULTIBAR(w),0);

  sprintf(buffer,"%3.1f:1",1./val);

  if(val==1.)
    sprintf(buffer,"off");

  if(val==.1)
    sprintf(buffer,"gate");

  readout_set(r,buffer);
  compand_s_setratio(val);
}

static void suppress_depth_change(GtkWidget *w,gpointer in){
  char buffer[80];
  Readout *r=READOUT(in);
  gdouble val=-140.-multibar_get_value(MULTIBAR(w),0);
  
  sprintf(buffer,"%3.0fdB",-val);

  if(val==0.)
    sprintf(buffer,"off");

  if(val==-140)
    sprintf(buffer,"deep");

  readout_set(r,buffer);
  compand_s_setdepth(val);
}

static void envelope_compander_change(GtkWidget *w,gpointer in){
  char buffer[80];
  Readout *r=READOUT(in);
  gdouble val=multibar_get_value(MULTIBAR(w),0);

  sprintf(buffer,"%3.1f%%",val);
  readout_set(r,buffer);
  compand_ec_set(val);
}

static void slider_change(GtkWidget *w,gpointer in){
  char buffer[80];
  cbar *b=(cbar *)in;

  gdouble val=multibar_get_value(MULTIBAR(b->slider),0);
  sprintf(buffer,"%3.0fdB",val);
  readout_set(READOUT(b->readoutg),buffer);
  compand_g_set(b->number,val);
  
  val=multibar_get_value(MULTIBAR(b->slider),1);
  sprintf(buffer,"%+4.0fdB",val);
  readout_set(READOUT(b->readoute),buffer);
  compand_e_set(b->number,val);

  val=multibar_get_value(MULTIBAR(b->slider),2);
  sprintf(buffer,"%+4.0fdB",val);
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
  
  GtkWidget *hbox=gtk_hbox_new(0,0);
  GtkWidget *staticbox=gtk_vbox_new(0,0);
  GtkWidget *envelopeframe=gtk_frame_new(" Envelope Compander ");
  GtkWidget *suppressframe=gtk_frame_new(" Suppressor ");

  GtkWidget *envelopetable=gtk_table_new(2,3,0);
  GtkWidget *suppresstable=gtk_table_new(5,3,0);

  GtkWidget *slidertable=gtk_table_new(freqs+1,5,0);
  

  GtkWidget *link_box=gtk_hbox_new(0,0);
  GtkWidget *link_check=gtk_check_button_new_with_mnemonic("_link channel envelopes");
  gtk_box_pack_end(GTK_BOX(link_box),link_check,0,0,0);  
  gtk_box_pack_end(GTK_BOX(staticbox),link_box,0,0,0);
  gtk_container_set_border_width(GTK_CONTAINER(link_box),5);
  g_signal_connect (G_OBJECT (link_check), "toggled",
		    G_CALLBACK (link_toggled), (gpointer)0);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(link_check),1);


  gtk_box_pack_start(GTK_BOX(panel->subpanel_box),hbox,0,0,0);
  gtk_box_pack_start(GTK_BOX(hbox),slidertable,0,0,0);
  gtk_box_pack_start(GTK_BOX(hbox),staticbox,0,0,0);

  gtk_box_pack_end(GTK_BOX(staticbox),suppressframe,0,0,0);
  gtk_box_pack_end(GTK_BOX(staticbox),envelopeframe,0,0,0);
  gtk_container_add(GTK_CONTAINER(envelopeframe),envelopetable);
  gtk_container_add(GTK_CONTAINER(suppressframe),suppresstable);

  gtk_container_set_border_width(GTK_CONTAINER(suppressframe),5);
  gtk_container_set_border_width(GTK_CONTAINER(envelopeframe),5);
  gtk_container_set_border_width(GTK_CONTAINER(envelopeframe),5);

  /* envelope compand */
  {
    double levels[11]={-50,-40,-30,-20,-10,0,10,20,30,40,50};
    char *labels[10]={"40%","30%","20%","10%","0%","10%","20%",
		      "30%","40%","50%"};
    GtkWidget *label1=gtk_label_new("compress");
    GtkWidget *label2=gtk_label_new("expand");
    GtkWidget *envelope_compress_readout=readout_new(" 0.0%");
    GtkWidget *envelope_compress_slider=multibar_slider_new(10,labels,levels,1);
    
    gtk_container_set_border_width(GTK_CONTAINER(envelopetable),4);
    multibar_callback(MULTIBAR(envelope_compress_slider),
		      envelope_compander_change,
		      envelope_compress_readout);
    multibar_thumb_increment(MULTIBAR(envelope_compress_slider),.1,1.);

    gtk_widget_set_name(label1,"smallmarker");
    gtk_widget_set_name(label2,"smallmarker");
    gtk_misc_set_alignment(GTK_MISC(label1),0,.5);
    gtk_misc_set_alignment(GTK_MISC(label2),1,.5);

    gtk_table_attach(GTK_TABLE(envelopetable),label1,0,1,1,2,GTK_FILL,0,2,0);
    gtk_table_attach(GTK_TABLE(envelopetable),label2,1,2,1,2,GTK_FILL,0,2,0);
    gtk_table_attach(GTK_TABLE(envelopetable),envelope_compress_slider,
		     0,2,0,1,GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,0,0);
    gtk_table_attach(GTK_TABLE(envelopetable),envelope_compress_readout,
		     2,3,0,1,GTK_FILL,0,0,2);

  }

  /* suppressor */
  {

    double bias_levels[7]={-30,-20,-10,0,10,20,30};
    char  *bias_labels[6]={"20","10","0","10","20","30"};

    double decay_levels[7]={-1.,-.5,-.2,-.1,-.05,-.02,-.01};
    char  *decay_labels[6]={".5",".2",".1",".05",".02","slow"};

    double ratio_levels[7]={-1.,-.67,-.5,-.33,-.25,-.17,-.1};
    char  *ratio_labels[6]={"1.5:1","2:1","3:1","4:1","6:1","gate"};

    double depth_levels[7]={-140,-130,-120,-110,-100,-80,-0};
    char  *depth_labels[6]={"10","20","30","40","60","deep"};

    GtkWidget *suppress_bias_slider=multibar_slider_new(6,bias_labels,bias_levels,1);
    GtkWidget *suppress_decay_slider=multibar_slider_new(6,decay_labels,decay_levels,1);
    GtkWidget *suppress_ratio_slider=multibar_slider_new(6,ratio_labels,ratio_levels,1);
    GtkWidget *suppress_depth_slider=multibar_slider_new(6,depth_labels,depth_levels,1);
    GtkWidget *suppress_bias_readout=readout_new(" +0dB");
    GtkWidget *suppress_decay_readout=readout_new("fast");
    GtkWidget *suppress_ratio_readout=readout_new("1.5:1");
    GtkWidget *suppress_depth_readout=readout_new("gate");

    GtkWidget *label1=gtk_label_new("mode:");
    GtkWidget *label2=gtk_label_new("bias:");
    GtkWidget *label3=gtk_label_new("decay:");
    GtkWidget *label4=gtk_label_new("ratio:");
    GtkWidget *label5=gtk_label_new("depth:");
    
    GtkWidget *envelopebox=gtk_hbox_new(0,0);
    GtkWidget *suppress_rms_button=gtk_radio_button_new_with_label(NULL,"RMS");
    GtkWidget *suppress_peak_button=
      gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(suppress_rms_button),
						  "peak");
    gtk_box_pack_start(GTK_BOX(envelopebox),suppress_rms_button,0,0,5);
    gtk_box_pack_start(GTK_BOX(envelopebox),suppress_peak_button,0,0,5);


    g_signal_connect (G_OBJECT (suppress_rms_button), "clicked",
		      G_CALLBACK (suppressor_mode), (gpointer)0); //To Hell I Go
    g_signal_connect (G_OBJECT (suppress_peak_button), "clicked",
		      G_CALLBACK (suppressor_mode), (gpointer)1);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(suppress_rms_button),1);


    gtk_container_set_border_width(GTK_CONTAINER(suppresstable),4);

    gtk_misc_set_alignment(GTK_MISC(label1),1,.5);
    gtk_misc_set_alignment(GTK_MISC(label2),1,.5);
    gtk_misc_set_alignment(GTK_MISC(label3),1,.5);
    gtk_misc_set_alignment(GTK_MISC(label4),1,.5);
    gtk_misc_set_alignment(GTK_MISC(label5),1,.5);


    gtk_table_attach(GTK_TABLE(suppresstable),label1,0,1,0,1,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(suppresstable),label2,0,1,1,2,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(suppresstable),label3,0,1,2,3,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(suppresstable),label4,0,1,3,4,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(suppresstable),label5,0,1,4,5,GTK_FILL,0,0,0);

    gtk_table_attach(GTK_TABLE(suppresstable),envelopebox,1,3,0,1,GTK_FILL,0,0,0);

    multibar_callback(MULTIBAR(suppress_bias_slider),
		      suppress_bias_change,
		      suppress_bias_readout);
    multibar_thumb_set(MULTIBAR(suppress_bias_slider),0.,0);

    multibar_callback(MULTIBAR(suppress_decay_slider),
		      suppress_decay_change,
		      suppress_decay_readout);
    multibar_thumb_set(MULTIBAR(suppress_decay_slider),-1.,0);

    multibar_callback(MULTIBAR(suppress_ratio_slider),
		      suppress_ratio_change,
		      suppress_ratio_readout);
    multibar_thumb_set(MULTIBAR(suppress_ratio_slider),-1.,0);

    multibar_callback(MULTIBAR(suppress_depth_slider),
		      suppress_depth_change,
		      suppress_depth_readout);
    multibar_thumb_set(MULTIBAR(suppress_depth_slider),0.,0);

    gtk_table_attach(GTK_TABLE(suppresstable),
		     suppress_bias_slider,1,2,1,2,GTK_FILL|GTK_EXPAND,
		     GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(suppresstable),
		     suppress_decay_slider,1,2,2,3,GTK_FILL|GTK_EXPAND,
		     GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(suppresstable),
		     suppress_ratio_slider,1,2,3,4,GTK_FILL|GTK_EXPAND,
		     GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(suppresstable),
		     suppress_depth_slider,1,2,4,5,GTK_FILL|GTK_EXPAND,
		     GTK_FILL|GTK_EXPAND,2,0);

    gtk_table_attach(GTK_TABLE(suppresstable),
		     suppress_bias_readout,2,3,1,2,GTK_FILL,0,0,2);
    gtk_table_attach(GTK_TABLE(suppresstable),
		     suppress_decay_readout,2,3,2,3,GTK_FILL,0,0,2);
    gtk_table_attach(GTK_TABLE(suppresstable),
		     suppress_ratio_readout,2,3,3,4,GTK_FILL,0,0,2);
    gtk_table_attach(GTK_TABLE(suppresstable),
		     suppress_depth_readout,2,3,4,5,GTK_FILL,0,0,2);

  }

  {
    GtkWidget *label2=gtk_label_new("gate");
    GtkWidget *label3=gtk_label_new("expd");
    GtkWidget *label4=gtk_label_new("cmpr");

    gtk_table_attach(GTK_TABLE(slidertable),label2,2,3,0,1,GTK_FILL,0,10,0);
    gtk_table_attach(GTK_TABLE(slidertable),label3,3,4,0,1,GTK_FILL,0,10,0);
    gtk_table_attach(GTK_TABLE(slidertable),label4,4,5,0,1,GTK_FILL,0,10,0);
    
  }

  for(i=0;i<freqs;i++){
    const char *labeltext=freq_frequency_label(i);

    GtkWidget *label=gtk_label_new(labeltext);
    gtk_widget_set_name(label,"smallmarker");

    bars[i].readoutg=readout_new("  +0dB");
    bars[i].readoute=readout_new("  +0dB");
    bars[i].readoutc=readout_new("  +0dB");
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

    gtk_table_attach(GTK_TABLE(slidertable),label,0,1,i+1,i+2,
		     GTK_FILL,0,10,0);
    gtk_table_attach(GTK_TABLE(slidertable),bars[i].readoutg,2,3,i+1,i+2,
		     GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(slidertable),bars[i].readoute,3,4,i+1,i+2,
		     GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(slidertable),bars[i].readoutc,4,5,i+1,i+2,
		     GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(slidertable),bars[i].slider,1,2,i+1,i+2,
		     GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,0,0);
  }

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

