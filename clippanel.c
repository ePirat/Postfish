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
#include "declip.h"

extern sig_atomic_t declip_active;
extern int input_ch;
extern int input_size;
extern int input_rate;
extern sig_atomic_t declip_converge;

GtkWidget **feedback_bars;

GtkWidget *samplereadout;
GtkWidget *msreadout;
GtkWidget *hzreadout;
GtkWidget *depth_readout;
GtkWidget *limit_readout;

GtkWidget *mainpanel_inbar;

typedef struct {
  GtkWidget *slider;
  GtkWidget *readout;
  GtkWidget *readoutdB;
  int number;
} clipslider;

static void trigger_slider_change(GtkWidget *w,gpointer in){
  char buffer[80];
  clipslider *p=(clipslider *)in;
  gdouble linear=multibar_get_value(MULTIBAR(p->slider),0);
  
  sprintf(buffer,"%1.2f",linear);
  readout_set(READOUT(p->readout),buffer);

  sprintf(buffer,"%3.0f dB",todB(linear));
  readout_set(READOUT(p->readoutdB),buffer);

  declip_settrigger(linear,p->number);

}

static void blocksize_slider_change(GtkWidget *w,gpointer in){
  char buffer[80];
  int choice=rint(gtk_range_get_value(GTK_RANGE(w)));
  int blocksize=64<<choice;

  sprintf(buffer,"%5d   ",blocksize);
  readout_set(READOUT(samplereadout),buffer);

  sprintf(buffer,"%3.1f ms",blocksize*1000./input_rate);
  readout_set(READOUT(msreadout),buffer);

  sprintf(buffer,"%5d Hz",(int)rint(input_rate*2./blocksize));
  readout_set(READOUT(hzreadout),buffer);
  
  declip_setblock(blocksize);
}

static void depth_slider_change(GtkWidget *w,gpointer in){
  char buffer[80];
  double dB=gtk_range_get_value(GTK_RANGE(w));

  sprintf(buffer,"%3ddB",(int)dB);
  readout_set(READOUT(depth_readout),buffer);

  declip_setconvergence(fromdB(-dB));
}

static void limit_slider_change(GtkWidget *w,gpointer in){
  char buffer[80];
  double percent=gtk_range_get_value(GTK_RANGE(w));

  sprintf(buffer,"%3d%%",(int)percent);
  readout_set(READOUT(limit_readout),buffer);

  declip_setiterations(percent*.01);
}

void clippanel_create(postfish_mainpanel *mp,
		      GtkWidget *windowbutton,
		      GtkWidget *activebutton){
  int i;
  char *labels[2]={"10%","100%"};
  double levels[3]={0.,10.,100.};
  int block_choices=0;

  subpanel_generic *panel=subpanel_create(mp,windowbutton,activebutton,
					  &declip_active,
					  "_Declipping filter setup"," [d] ");
  
  GtkWidget *framebox=gtk_hbox_new(1,0);
  GtkWidget *framebox_right=gtk_vbox_new(0,0);
  GtkWidget *blocksize_box=gtk_vbox_new(0,0);
  GtkWidget *blocksize_frame=gtk_frame_new (" filter width ");
  GtkWidget *converge_frame=gtk_frame_new (" filter convergence ");
  GtkWidget *limit_frame=gtk_frame_new (" filter CPU throttle ");
  GtkWidget *converge_box=gtk_vbox_new(0,0);
  GtkWidget *limit_box=gtk_vbox_new(0,0);
  GtkWidget *channel_table=gtk_table_new(input_ch,5,0);

  gtk_widget_set_name(blocksize_box,"choiceframe");
  gtk_widget_set_name(converge_box,"choiceframe");
  gtk_widget_set_name(limit_box,"choiceframe");
  gtk_container_set_border_width(GTK_CONTAINER(blocksize_box),2);
  gtk_container_set_border_width(GTK_CONTAINER(converge_box),2);
  gtk_container_set_border_width(GTK_CONTAINER(limit_box),2);

  feedback_bars=calloc(input_ch,sizeof(*feedback_bars));

  /* set up blocksize config */
  for(i=64;i<=input_size*2;i*=2)block_choices++;
  {
    GtkWidget *table=gtk_table_new(4,2,0);
    GtkWidget *sliderbox=gtk_hbox_new(0,0);
    GtkWidget *fastlabel=gtk_label_new("fastest");
    GtkWidget *qualitylabel=gtk_label_new("best");
    GtkWidget *slider=gtk_hscale_new_with_range(0,block_choices-1,1);
    GtkWidget *samplelabel=gtk_label_new("window sample width");
    GtkWidget *mslabel=gtk_label_new("window time width");
    GtkWidget *hzlabel=gtk_label_new("approximate lowest response");
    samplereadout=readout_new("00000   ");
    msreadout=readout_new("00000 ms");
    hzreadout=readout_new("00000 Hz");

    gtk_scale_set_draw_value(GTK_SCALE(slider),FALSE);
    gtk_misc_set_alignment(GTK_MISC(samplelabel),1,.5);
    gtk_misc_set_alignment(GTK_MISC(mslabel),1,.5);
    gtk_misc_set_alignment(GTK_MISC(hzlabel),1,.5);

    gtk_box_pack_start(GTK_BOX(sliderbox),fastlabel,0,0,4);
    gtk_box_pack_start(GTK_BOX(sliderbox),slider,1,1,0);
    gtk_box_pack_start(GTK_BOX(sliderbox),qualitylabel,0,0,4);
    gtk_table_attach(GTK_TABLE(table),sliderbox,0,2,0,1,GTK_FILL|GTK_EXPAND,0,0,8);
    gtk_table_attach(GTK_TABLE(table),samplelabel,0,1,1,2,GTK_FILL|GTK_EXPAND,GTK_FILL,0,0);
    gtk_table_attach(GTK_TABLE(table),mslabel,0,1,2,3,GTK_FILL|GTK_EXPAND,GTK_FILL,0,0);
    gtk_table_attach(GTK_TABLE(table),hzlabel,0,1,3,4,GTK_FILL|GTK_EXPAND,GTK_FILL,0,0);

    gtk_table_attach(GTK_TABLE(table),samplereadout,1,2,1,2,GTK_FILL,0,5,0);
    gtk_table_attach(GTK_TABLE(table),msreadout,1,2,2,3,GTK_FILL,0,5,0);
    gtk_table_attach(GTK_TABLE(table),hzreadout,1,2,3,4,GTK_FILL,0,5,0);
    gtk_container_add(GTK_CONTAINER(blocksize_box),table);


    g_signal_connect (G_OBJECT (slider), "key-press-event",
		      G_CALLBACK (slider_keymodify), NULL);
    g_signal_connect_after (G_OBJECT(slider), "value-changed",
			    G_CALLBACK(blocksize_slider_change), 0);
    gtk_range_set_value(GTK_RANGE(slider),2.);
    
  }
  gtk_container_add(GTK_CONTAINER(blocksize_frame),blocksize_box);

  /* set up convergence config */
  {
    GtkWidget *table=gtk_table_new(2,2,0);
    GtkWidget *sliderbox=gtk_hbox_new(0,0);
    GtkWidget *fastlabel=gtk_label_new("fastest");
    GtkWidget *qualitylabel=gtk_label_new("best");
    GtkWidget *slider=gtk_hscale_new_with_range(1,140,1);
    GtkWidget *label=gtk_label_new("solution depth");
    depth_readout=readout_new("000dB");

    gtk_scale_set_draw_value(GTK_SCALE(slider),FALSE);
    gtk_misc_set_alignment(GTK_MISC(label),1,.5);

    gtk_box_pack_start(GTK_BOX(sliderbox),fastlabel,0,0,4);
    gtk_box_pack_start(GTK_BOX(sliderbox),slider,1,1,0);
    gtk_box_pack_start(GTK_BOX(sliderbox),qualitylabel,0,0,4);
    gtk_table_attach(GTK_TABLE(table),sliderbox,0,2,0,1,GTK_FILL|GTK_EXPAND,0,0,8);
    gtk_table_attach(GTK_TABLE(table),label,0,1,1,2,GTK_FILL|GTK_EXPAND,GTK_FILL,0,0);

    gtk_table_attach(GTK_TABLE(table),depth_readout,1,2,1,2,GTK_FILL,0,5,0);

    gtk_container_add(GTK_CONTAINER(converge_box),table);

    g_signal_connect (G_OBJECT (slider), "key-press-event",
		      G_CALLBACK (slider_keymodify), NULL);
    g_signal_connect_after (G_OBJECT(slider), "value-changed",
			    G_CALLBACK(depth_slider_change), 0);
    gtk_range_set_value(GTK_RANGE(slider),60.);
  }


  /* set up limit config */
  {
    GtkWidget *table=gtk_table_new(2,2,0);
    GtkWidget *sliderbox=gtk_hbox_new(0,0);
    GtkWidget *fastlabel=gtk_label_new("fastest");
    GtkWidget *qualitylabel=gtk_label_new("best");
    GtkWidget *slider=gtk_hscale_new_with_range(1,100,1);
    GtkWidget *label=gtk_label_new("hard iteration limit");
    limit_readout=readout_new("000%");

    gtk_scale_set_draw_value(GTK_SCALE(slider),FALSE);
    gtk_misc_set_alignment(GTK_MISC(label),1,.5);

    gtk_box_pack_start(GTK_BOX(sliderbox),fastlabel,0,0,4);
    gtk_box_pack_start(GTK_BOX(sliderbox),slider,1,1,0);
    gtk_box_pack_start(GTK_BOX(sliderbox),qualitylabel,0,0,4);
    gtk_table_attach(GTK_TABLE(table),sliderbox,0,2,0,1,GTK_FILL|GTK_EXPAND,0,0,8);
    gtk_table_attach(GTK_TABLE(table),label,0,1,1,2,GTK_FILL|GTK_EXPAND,GTK_FILL,0,0);

    gtk_table_attach(GTK_TABLE(table),limit_readout,1,2,1,2,GTK_FILL,0,5,0);

    gtk_container_add(GTK_CONTAINER(limit_box),table);

    g_signal_connect (G_OBJECT (slider), "key-press-event",
		      G_CALLBACK (slider_keymodify), NULL);
    g_signal_connect_after (G_OBJECT(slider), "value-changed",
			    G_CALLBACK(limit_slider_change), 0);
    gtk_range_set_value(GTK_RANGE(slider),100.);
  }

  for(i=0;i<input_ch;i++){
    char *slabels[8]={".05",".1",".2",".3",".4",
		      ".6",".8","1."};
    double slevels[9]={.01,.05,.1,.2,.3,.4,.6,
                       .8,1.};

    char buffer[80];
    clipslider *cs=calloc(1,sizeof(*cs));
    GtkWidget *label;
    GtkWidget *slider=multibar_new(8,slabels,slevels,HI_DECAY|ZERO_DAMP|PEAK_FOLLOW);
    GtkWidget *readout=readout_new("0.00");
    GtkWidget *readoutdB=readout_new("-40 dB");
    GtkWidget *bar=multibar_new(2,labels,levels,HI_DECAY|ZERO_DAMP|PEAK_FOLLOW);

    cs->slider=slider;
    cs->readout=readout;
    cs->readoutdB=readoutdB;
    cs->number=i;
    feedback_bars[i]=bar;

    gtk_widget_set_name(bar,"clipbar");
    multibar_thumb_set(MULTIBAR(slider),1.,0);
    multibar_thumb_bounds(MULTIBAR(slider),.01,1.);

    switch(input_ch){
    case 1:
      sprintf(buffer,"trigger level:");
      break;
    case 2:
      switch(i){
      case 0:
	sprintf(buffer,"left trigger level:");
	break;
      case 1:
	sprintf(buffer,"right trigger level:");
	break;
      }
      break;
    default:
      sprintf(buffer,"%d trigger level:",i+1);
    }
    label=gtk_label_new(buffer);
    gtk_misc_set_alignment(GTK_MISC(label),1,.5);

    gtk_table_attach(GTK_TABLE(channel_table),label,0,1,i,i+1,GTK_FILL,GTK_FILL,2,0);
    gtk_table_attach(GTK_TABLE(channel_table),readout,1,2,i,i+1,GTK_FILL,GTK_FILL,0,0);
    gtk_table_attach(GTK_TABLE(channel_table),readoutdB,2,3,i,i+1,GTK_FILL,GTK_FILL,0,0);
    gtk_table_attach(GTK_TABLE(channel_table),slider,3,4,i,i+1,GTK_FILL|GTK_EXPAND,GTK_FILL,0,0);
    gtk_table_attach(GTK_TABLE(channel_table),bar,4,5,i,i+1,GTK_FILL,GTK_FILL,0,0);

    multibar_callback(MULTIBAR(slider),trigger_slider_change,(gpointer)cs);

    trigger_slider_change(NULL,cs);
  }

  gtk_container_add(GTK_CONTAINER(converge_frame),converge_box);
  gtk_container_add(GTK_CONTAINER(limit_frame),limit_box);

  gtk_box_pack_start(GTK_BOX(framebox),blocksize_frame,1,1,4);
  gtk_box_pack_start(GTK_BOX(framebox),framebox_right,1,1,4);

  gtk_box_pack_start(GTK_BOX(framebox_right),converge_frame,1,1,0);
  gtk_box_pack_start(GTK_BOX(framebox_right),limit_frame,1,1,0);

  gtk_box_pack_start(GTK_BOX(panel->subpanel_box),framebox,1,1,4);
  gtk_box_pack_start(GTK_BOX(panel->subpanel_box),channel_table,1,1,4);


  mainpanel_inbar=mp->inbar;

}

void clippanel_feedback(void){
  int clip[input_ch],count;
  if(pull_declip_feedback(clip,&count)){
    int i;
    for(i=0;i<input_ch;i++){
      double val[2],zero[2];
      val[0]=-1.,zero[0]=-1.;
      val[1]=(count?clip[i]*100./count-.1:-1);
      zero[1]=-1.;
      multibar_set(MULTIBAR(feedback_bars[i]),zero,val,2);
      if(clip[i]){
	multibar_setwarn(MULTIBAR(mainpanel_inbar));
	multibar_setwarn(MULTIBAR(feedback_bars[i]));
      }
    }
  }
}

void clippanel_reset(void){
  int i;
  for(i=0;i<input_ch;i++)
    multibar_reset(MULTIBAR(feedback_bars[i]));
}
