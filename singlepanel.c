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
#include "singlecomp.h"
#include "singlepanel.h"

extern sig_atomic_t singlecomp_active;
extern sig_atomic_t singlecomp_visible;
extern int input_ch;
extern int input_size;
extern int input_rate;

extern singlecomp_settings scset;

typedef struct {
  GtkWidget *r0;
  GtkWidget *r1;
} multireadout;

GtkWidget *t_label;
GtkWidget *t_slider;
multireadout t_readout;


static void compand_change(GtkWidget *w,Readout *r,sig_atomic_t *var){
  char buffer[80];
  float val=1./multibar_get_value(MULTIBAR(w),0);

  if(val==1.){
    sprintf(buffer,"   off");
  }else if(val>=10){
    sprintf(buffer,"%4.1f:1",val);
  }else if(val>=1){
    sprintf(buffer,"%4.2f:1",val);
  }else if(val>.10001){
    sprintf(buffer,"1:%4.2f",1./val);
  }else{
    sprintf(buffer,"1:%4.1f",1./val);
  }

  readout_set(r,buffer);
  
  *var=rint(val*1000.);
}
static void under_compand_change(GtkWidget *w,gpointer in){
  compand_change(w,(Readout *)in,&scset.u_ratio);
}

static void over_compand_change(GtkWidget *w,gpointer in){
  compand_change(w,(Readout *)in,&scset.o_ratio);
}

static void base_compand_change(GtkWidget *w,gpointer in){
  compand_change(w,(Readout *)in,&scset.b_ratio);
}

static void timing_display(GtkWidget *w,GtkWidget *r,float v){
  char buffer[80];

  if(v<10){
    sprintf(buffer,"%4.2fms",v);
  }else if(v<100){
    sprintf(buffer,"%4.1fms",v);
  }else if (v<1000){
    sprintf(buffer,"%4.0fms",v);
  }else if (v<10000){
    sprintf(buffer,"%4.2fs",v/1000.);
  }else{
    sprintf(buffer,"%4.1fs",v/1000.);
  }

  readout_set(READOUT(r),buffer);
}

static void under_timing_change(GtkWidget *w,gpointer in){
  multireadout *r=(multireadout *)in;
  float attack=multibar_get_value(MULTIBAR(w),0);
  float decay=multibar_get_value(MULTIBAR(w),1);

  timing_display(w,r->r0,attack);
  timing_display(w,r->r1,decay);

  scset.u_attack=rint(attack*10.);
  scset.u_decay=rint(decay*10.);
}

static void over_timing_change(GtkWidget *w,gpointer in){
  multireadout *r=(multireadout *)in;
  float attack=multibar_get_value(MULTIBAR(w),0);
  float decay=multibar_get_value(MULTIBAR(w),1);

  timing_display(w,r->r0,attack);
  timing_display(w,r->r1,decay);

  scset.o_attack=rint(attack*10.);
  scset.o_decay=rint(decay*10.);
}

static void base_timing_change(GtkWidget *w,gpointer in){
  multireadout *r=(multireadout *)in;
  float attack=multibar_get_value(MULTIBAR(w),0);
  float decay=multibar_get_value(MULTIBAR(w),1);

  timing_display(w,r->r0,attack);
  timing_display(w,r->r1,decay);

  scset.b_attack=rint(attack*10.);
  scset.b_decay=rint(decay*10.);
}

static void under_lookahead_change(GtkWidget *w,gpointer in){
  char buffer[80];
  Readout *r=(Readout *)in;
  float val=multibar_get_value(MULTIBAR(w),0);

  sprintf(buffer,"%3.0f%%",val);
  readout_set(r,buffer);
  
  scset.u_lookahead=rint(val*10.);
}

static void over_lookahead_change(GtkWidget *w,gpointer in){
  char buffer[80];
  Readout *r=(Readout *)in;
  float val=multibar_get_value(MULTIBAR(w),0);

  sprintf(buffer,"%3.0f%%",val);
  readout_set(r,buffer);
  
  scset.o_lookahead=rint(val*10.);
}

static void slider_change(GtkWidget *w,gpointer in){
  char buffer[80];
  multireadout *r=(multireadout *)in;
  int o,u;

  u=multibar_get_value(MULTIBAR(w),0);
  sprintf(buffer,"%+4ddB",u);
  readout_set(READOUT(r->r0),buffer);
  scset.u_thresh=u;
  
  o=multibar_get_value(MULTIBAR(w),1);
  sprintf(buffer,"%+4ddB",o);
  readout_set(READOUT(r->r1),buffer);
  scset.o_thresh=o;
}

static void over_mode(GtkButton *b,gpointer in){
  int mode=(int)in;
  scset.o_mode=mode;
}

static void under_mode(GtkButton *b,gpointer in){
  int mode=(int)in;
  scset.u_mode=mode;
}

static void base_mode(GtkButton *b,gpointer in){
  int mode=(int)in;
  scset.b_mode=mode;
}

static void under_knee(GtkToggleButton *b,gpointer in){
  int mode=gtk_toggle_button_get_active(b);
  scset.u_softknee=mode;
}

static void over_knee(GtkToggleButton *b,gpointer in){
  int mode=gtk_toggle_button_get_active(b);
  scset.o_softknee=mode;
}

void singlepanel_create(postfish_mainpanel *mp,
			GtkWidget *windowbutton,
			GtkWidget *activebutton){

  char *labels[14]={"130","120","110","100","90","80","70",
		    "60","50","40","30","20","10","0"};
  float levels[15]={-140,-130,-120,-110,-100,-90,-80,-70,-60,-50,-40,
		     -30,-20,-10,0};

  float compand_levels[9]={.1,.25,.5,.6667,1,1.5,2,4,10};
  char  *compand_labels[8]={"4:1","2:1","1:1.5","1:1","1:1.5","1:2","1:4","1:10"};

  float timing_levels[6]={.5,1,10,100,1000,10000};
  char  *timing_labels[5]={"1ms","10ms","100ms","1s","10s"};

  float per_levels[9]={0,12.5,25,37.5,50,62.5,75,87.5,100};
  char  *per_labels[8]={"","25%","","50%","","75%","","100%"};

  char *shortcut[]={" o "};

  subpanel_generic *panel=subpanel_create(mp,windowbutton,&activebutton,
					  &singlecomp_active,
					  &singlecomp_visible,
					  "_Oneband Compand",shortcut,
					  0,1);
  
  GtkWidget *sliderframe=gtk_frame_new(NULL);
  GtkWidget *allbox=gtk_vbox_new(0,0);
  GtkWidget *slidertable=gtk_table_new(2,3,0);

  GtkWidget *overlabel=gtk_label_new("Over threshold compand ");
  GtkWidget *overtable=gtk_table_new(6,4,0);

  GtkWidget *underlabel=gtk_label_new("Under threshold compand ");
  GtkWidget *undertable=gtk_table_new(5,4,0);

  GtkWidget *baselabel=gtk_label_new("Global compand ");
  GtkWidget *basetable=gtk_table_new(3,4,0);

  gtk_widget_set_name(overlabel,"framelabel");
  gtk_widget_set_name(underlabel,"framelabel");
  gtk_widget_set_name(baselabel,"framelabel");

  gtk_misc_set_alignment(GTK_MISC(overlabel),0,.5);
  gtk_misc_set_alignment(GTK_MISC(underlabel),0,.5);
  gtk_misc_set_alignment(GTK_MISC(baselabel),0,.5);

    
  {
    GtkWidget *label1=gtk_label_new("compand threshold");
    GtkWidget *label2=gtk_label_new("under");
    GtkWidget *label3=gtk_label_new("over");

    gtk_misc_set_alignment(GTK_MISC(label1),.5,1.);
    gtk_misc_set_alignment(GTK_MISC(label2),.5,1.);
    gtk_misc_set_alignment(GTK_MISC(label3),.5,1.);
    gtk_widget_set_name(label2,"scalemarker");
    gtk_widget_set_name(label3,"scalemarker");

    gtk_table_attach(GTK_TABLE(slidertable),label2,0,1,0,1,GTK_FILL,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(slidertable),label1,1,2,0,1,GTK_FILL,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(slidertable),label3,2,3,0,1,GTK_FILL,GTK_FILL|GTK_EXPAND,2,0);
 
    gtk_container_add(GTK_CONTAINER(sliderframe),slidertable);

    //gtk_frame_set_shadow_type(GTK_FRAME(sliderframe),GTK_SHADOW_NONE);
    
    gtk_container_set_border_width(GTK_CONTAINER(sliderframe),4);
    gtk_container_set_border_width(GTK_CONTAINER(slidertable),10);

  }

  gtk_box_pack_start(GTK_BOX(panel->subpanel_box),allbox,0,0,0);
  gtk_box_pack_start(GTK_BOX(allbox),sliderframe,0,0,0);


  {
    GtkWidget *hs1=gtk_hseparator_new();
    GtkWidget *hs2=gtk_hseparator_new();

    //gtk_box_pack_start(GTK_BOX(allbox),hs3,0,0,0);
    gtk_box_pack_start(GTK_BOX(allbox),overtable,0,0,10);
    gtk_box_pack_start(GTK_BOX(allbox),hs1,0,0,0);
    gtk_box_pack_start(GTK_BOX(allbox),undertable,0,0,10);
    gtk_box_pack_start(GTK_BOX(allbox),hs2,0,0,0);
    gtk_box_pack_start(GTK_BOX(allbox),basetable,0,0,10);

    gtk_container_set_border_width(GTK_CONTAINER(overtable),5);
    gtk_container_set_border_width(GTK_CONTAINER(undertable),5);
    gtk_container_set_border_width(GTK_CONTAINER(basetable),5);

  }

  /* under compand: mode and knee */
  {
    GtkWidget *envelopebox=gtk_hbox_new(0,0);
    GtkWidget *rms_button=gtk_radio_button_new_with_label(NULL,"RMS");
    GtkWidget *peak_button=
      gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(rms_button),
						  "peak");
    GtkWidget *knee_button=gtk_check_button_new_with_label("soft knee");

    gtk_box_pack_start(GTK_BOX(envelopebox),underlabel,0,0,0);
    gtk_box_pack_end(GTK_BOX(envelopebox),peak_button,0,0,5);
    gtk_box_pack_end(GTK_BOX(envelopebox),rms_button,0,0,5);
    gtk_box_pack_end(GTK_BOX(envelopebox),knee_button,0,0,5);

    g_signal_connect (G_OBJECT (knee_button), "clicked",
		      G_CALLBACK (under_knee), (gpointer)0);
    g_signal_connect (G_OBJECT (rms_button), "clicked",
		      G_CALLBACK (under_mode), (gpointer)0);
    g_signal_connect (G_OBJECT (peak_button), "clicked",
		      G_CALLBACK (under_mode), (gpointer)1); //To Hell I Go
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rms_button),1);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(knee_button),1);
    gtk_table_attach(GTK_TABLE(undertable),envelopebox,0,4,0,1,GTK_FILL,0,0,0);
  }

  /* under compand: ratio */
  {

    GtkWidget *label=gtk_label_new("compand ratio:");
    GtkWidget *readout=readout_new("1.55:1");
    GtkWidget *slider=multibar_slider_new(8,compand_labels,compand_levels,1);
   
    multibar_callback(MULTIBAR(slider),under_compand_change,readout);
    multibar_thumb_set(MULTIBAR(slider),1.,0);

    gtk_misc_set_alignment(GTK_MISC(label),1.,.5);

    gtk_table_set_row_spacing(GTK_TABLE(undertable),0,4);
    gtk_table_attach(GTK_TABLE(undertable),label,0,1,1,2,GTK_FILL,0,2,0);
    gtk_table_attach(GTK_TABLE(undertable),slider,1,3,1,2,GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(undertable),readout,3,4,1,2,GTK_FILL,0,0,0);

  }

  /* under compand: timing */
  {

    GtkWidget *label=gtk_label_new("attack/decay:");
    GtkWidget *readout1=readout_new(" 100ms");
    GtkWidget *readout2=readout_new(" 100ms");
    GtkWidget *slider=multibar_slider_new(5,timing_labels,timing_levels,2);
    multireadout *r=calloc(1,sizeof(*r));

    r->r0=readout1;
    r->r1=readout2;
   
    multibar_callback(MULTIBAR(slider),under_timing_change,r);
    multibar_thumb_set(MULTIBAR(slider),1,0);
    multibar_thumb_set(MULTIBAR(slider),100,1);

    gtk_misc_set_alignment(GTK_MISC(label),1,.5);

    gtk_table_set_row_spacing(GTK_TABLE(undertable),2,4);
    gtk_table_attach(GTK_TABLE(undertable),label,0,1,4,5,GTK_FILL,0,2,0);
    gtk_table_attach(GTK_TABLE(undertable),slider,1,2,4,5,GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(undertable),readout1,2,3,4,5,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(undertable),readout2,3,4,4,5,GTK_FILL,0,0,0);

  }

  /* under compand: lookahead */
  {

    GtkWidget *label=gtk_label_new("lookahead:");
    GtkWidget *readout=readout_new("100%");
    GtkWidget *slider=multibar_slider_new(8,per_labels,per_levels,1);
    
    multibar_callback(MULTIBAR(slider),under_lookahead_change,readout);
    multibar_thumb_set(MULTIBAR(slider),100.,0);
    multibar_thumb_increment(MULTIBAR(slider),1.,10.);

    gtk_misc_set_alignment(GTK_MISC(label),1,.5);
    
    gtk_table_set_row_spacing(GTK_TABLE(undertable),3,4);
    gtk_table_attach(GTK_TABLE(undertable),label,0,1,3,4,GTK_FILL,0,2,0);
    gtk_table_attach(GTK_TABLE(undertable),slider,1,3,3,4,GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(undertable),readout,3,4,3,4,GTK_FILL,0,0,0);
  }

  /* over compand: mode and knee */
  {
    GtkWidget *envelopebox=gtk_hbox_new(0,0);
    GtkWidget *rms_button=gtk_radio_button_new_with_label(NULL,"RMS");
    GtkWidget *peak_button=
      gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(rms_button),
						  "peak");
    GtkWidget *knee_button=gtk_check_button_new_with_label("soft knee");

    gtk_box_pack_start(GTK_BOX(envelopebox),overlabel,0,0,0);
    gtk_box_pack_end(GTK_BOX(envelopebox),peak_button,0,0,5);
    gtk_box_pack_end(GTK_BOX(envelopebox),rms_button,0,0,5);
    gtk_box_pack_end(GTK_BOX(envelopebox),knee_button,0,0,5);

    g_signal_connect (G_OBJECT (knee_button), "clicked",
		      G_CALLBACK (over_knee), (gpointer)0);
    g_signal_connect (G_OBJECT (rms_button), "clicked",
		      G_CALLBACK (over_mode), (gpointer)0);
    g_signal_connect (G_OBJECT (peak_button), "clicked",
		      G_CALLBACK (over_mode), (gpointer)1); //To Hell I Go
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rms_button),1);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(knee_button),1);
    gtk_table_attach(GTK_TABLE(overtable),envelopebox,0,4,0,1,GTK_FILL,0,0,0);
  }

  /* over compand: ratio */
  {

    GtkWidget *label=gtk_label_new("compand ratio:");
    GtkWidget *readout=readout_new("1.55:1");
    GtkWidget *slider=multibar_slider_new(8,compand_labels,compand_levels,1);
   
    multibar_callback(MULTIBAR(slider),over_compand_change,readout);
    multibar_thumb_set(MULTIBAR(slider),1.,0);

    gtk_misc_set_alignment(GTK_MISC(label),1.,.5);

    gtk_table_set_row_spacing(GTK_TABLE(overtable),0,4);
    gtk_table_attach(GTK_TABLE(overtable),label,0,1,1,2,GTK_FILL,0,2,0);
    gtk_table_attach(GTK_TABLE(overtable),slider,1,3,1,2,GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(overtable),readout,3,4,1,2,GTK_FILL,0,0,0);

  }

  /* over compand: timing */
  {

    GtkWidget *label=gtk_label_new("attack/decay:");
    GtkWidget *readout1=readout_new(" 100ms");
    GtkWidget *readout2=readout_new(" 100ms");
    GtkWidget *slider=multibar_slider_new(5,timing_labels,timing_levels,2);
   
    multireadout *r=calloc(1,sizeof(*r));
    r->r0=readout1;
    r->r1=readout2;

    multibar_callback(MULTIBAR(slider),over_timing_change,r);
    multibar_thumb_set(MULTIBAR(slider),1,0);
    multibar_thumb_set(MULTIBAR(slider),100,1);

    gtk_misc_set_alignment(GTK_MISC(label),1,.5);

    gtk_table_set_row_spacing(GTK_TABLE(overtable),2,4);
    gtk_table_attach(GTK_TABLE(overtable),label,0,1,5,6,GTK_FILL,0,2,0);
    gtk_table_attach(GTK_TABLE(overtable),slider,1,2,5,6,GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(overtable),readout1,2,3,5,6,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(overtable),readout2,3,4,5,6,GTK_FILL,0,0,0);

  }

  /* over compand: lookahead */
  {

    GtkWidget *label=gtk_label_new("lookahead:");
    GtkWidget *readout=readout_new("100%");
    GtkWidget *slider=multibar_slider_new(8,per_labels,per_levels,1);
   
    multibar_callback(MULTIBAR(slider),over_lookahead_change,readout);
    multibar_thumb_set(MULTIBAR(slider),100.,0);
    multibar_thumb_increment(MULTIBAR(slider),1.,10.);

    gtk_misc_set_alignment(GTK_MISC(label),1,.5);
    
    gtk_table_set_row_spacing(GTK_TABLE(overtable),3,4);
    gtk_table_attach(GTK_TABLE(overtable),label,0,1,3,4,GTK_FILL,0,2,0);
    gtk_table_attach(GTK_TABLE(overtable),slider,1,3,3,4,GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(overtable),readout,3,4,3,4,GTK_FILL,0,0,0);
  }


  /* base compand: mode */
  {
    GtkWidget *envelopebox=gtk_hbox_new(0,0);
    GtkWidget *rms_button=gtk_radio_button_new_with_label(NULL,"RMS");
    GtkWidget *peak_button=
      gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(rms_button),
						  "peak");

    gtk_box_pack_start(GTK_BOX(envelopebox),baselabel,0,0,0);
    gtk_box_pack_end(GTK_BOX(envelopebox),peak_button,0,0,5);
    gtk_box_pack_end(GTK_BOX(envelopebox),rms_button,0,0,5);

    g_signal_connect (G_OBJECT (rms_button), "clicked",
		      G_CALLBACK (base_mode), (gpointer)0);
    g_signal_connect (G_OBJECT (peak_button), "clicked",
		      G_CALLBACK (base_mode), (gpointer)1); //To Hell I Go
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rms_button),1);
    gtk_table_attach(GTK_TABLE(basetable),envelopebox,0,4,0,1,GTK_FILL,0,0,0);
  }

  /* base compand: ratio */
  {

    GtkWidget *label=gtk_label_new("compand ratio:");
    GtkWidget *readout=readout_new("1.55:1");
    GtkWidget *slider=multibar_slider_new(8,compand_labels,compand_levels,1);
   
    multibar_callback(MULTIBAR(slider),base_compand_change,readout);
    multibar_thumb_set(MULTIBAR(slider),1.,0);

    gtk_misc_set_alignment(GTK_MISC(label),1.,.5);

    gtk_table_set_row_spacing(GTK_TABLE(basetable),0,4);
    gtk_table_attach(GTK_TABLE(basetable),label,0,1,1,2,GTK_FILL,0,2,0);
    gtk_table_attach(GTK_TABLE(basetable),slider,1,3,1,2,GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(basetable),readout,3,4,1,2,GTK_FILL,0,0,0);

  }

  /* base compand: timing */
  {

    GtkWidget *label=gtk_label_new("attack/decay:");
    GtkWidget *readout1=readout_new(" 100ms");
    GtkWidget *readout2=readout_new(" 100ms");
    GtkWidget *slider=multibar_slider_new(5,timing_labels,timing_levels,2);
    multireadout *r=calloc(1,sizeof(*r));

    r->r0=readout1;
    r->r1=readout2;
   
    multibar_callback(MULTIBAR(slider),base_timing_change,r);
    multibar_thumb_set(MULTIBAR(slider),1,0);
    multibar_thumb_set(MULTIBAR(slider),100,1);

    gtk_misc_set_alignment(GTK_MISC(label),1,.5);

    gtk_table_set_row_spacing(GTK_TABLE(basetable),2,4);
    gtk_table_attach(GTK_TABLE(basetable),label,0,1,4,5,GTK_FILL,0,2,0);
    gtk_table_attach(GTK_TABLE(basetable),slider,1,2,4,5,GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(basetable),readout1,2,3,4,5,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(basetable),readout2,3,4,4,5,GTK_FILL,0,0,0);

  }

  /* threshold controls */

  {
    t_readout.r0=readout_new("  +0");
    t_readout.r1=readout_new("  +0");
    t_slider=multibar_new(14,labels,levels,2,HI_DECAY|LO_DECAY|LO_ATTACK);

    multibar_callback(MULTIBAR(t_slider),slider_change,&t_readout);
    multibar_thumb_set(MULTIBAR(t_slider),-140.,0);
    multibar_thumb_set(MULTIBAR(t_slider),0.,1);
    multibar_thumb_bounds(MULTIBAR(t_slider),-140,0);
    multibar_thumb_increment(MULTIBAR(t_slider),1.,10.);
    
    
    gtk_table_attach(GTK_TABLE(slidertable),t_readout.r0,0,1,1,2,
		     0,0,0,0);
    gtk_table_attach(GTK_TABLE(slidertable),t_slider,1,2,1,2,
		     GTK_FILL|GTK_EXPAND,GTK_EXPAND,0,0);
    gtk_table_attach(GTK_TABLE(slidertable),t_readout.r1,2,3,1,2,
		     0,0,0,0);
  }
  
  subpanel_show_all_but_toplevel(panel);

}

static float *peakfeed=0;
static float *rmsfeed=0;

void singlepanel_feedback(int displayit){
  if(!peakfeed){
    peakfeed=malloc(sizeof(*peakfeed)*input_ch);
    rmsfeed=malloc(sizeof(*rmsfeed)*input_ch);
  }
  
  if(pull_singlecomp_feedback(peakfeed,rmsfeed)==1)
    multibar_set(MULTIBAR(t_slider),rmsfeed,peakfeed,
		 input_ch,(displayit && singlecomp_visible));
}

void singlepanel_reset(void){
  multibar_reset(MULTIBAR(t_slider));
}



