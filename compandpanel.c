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
#include "multicompand.h"
#include "compandpanel.h"

extern sig_atomic_t compand_active;
extern sig_atomic_t compand_visible;
extern int input_ch;
extern int input_size;
extern int input_rate;

extern banked_compand_settings bc[multicomp_banks];
extern other_compand_settings c;

typedef struct {
  GtkWidget *label;
  GtkWidget *slider;
  GtkWidget *readoutg;
  GtkWidget *readoute;
  GtkWidget *readoutc;
  int number;
} cbar;

static int bank_active=2;
static cbar bars[multicomp_freqs_max];
static int inactive_updatep=1;

static void slider_change(GtkWidget *w,gpointer in){
  char buffer[80];
  cbar *b=(cbar *)in;
  float g,e,c;

  g=multibar_get_value(MULTIBAR(b->slider),0);
  sprintf(buffer,"%3.0f",g);
  readout_set(READOUT(b->readoutg),buffer);
  g=rint(g*10.);
  bc[bank_active].static_g[b->number]=g;
  
  e=multibar_get_value(MULTIBAR(b->slider),1);
  sprintf(buffer,"%+4.0f",e);
  readout_set(READOUT(b->readoute),buffer);
  e=rint(e*10.);
  bc[bank_active].static_e[b->number]=e;

  c=multibar_get_value(MULTIBAR(b->slider),2);
  sprintf(buffer,"%+4.0f",c);
  readout_set(READOUT(b->readoutc),buffer);
  c=rint(c*10.);
  bc[bank_active].static_c[b->number]=c;
  
  if(inactive_updatep){
    /* keep the inactive banks also tracking settings, but only where it
       makes sense */
    
    switch(bank_active){
    case 0:
      bc[1].static_g[b->number*2]=g;
      bc[1].static_e[b->number*2]=e;
      bc[1].static_c[b->number*2]=c;
      bc[2].static_g[b->number*3+1]=g;
      bc[2].static_e[b->number*3+1]=e;
      bc[2].static_c[b->number*3+1]=c;
      break;
    case 1:
      if((b->number&1)==0){
	bc[0].static_g[b->number>>1]=g;
	bc[0].static_e[b->number>>1]=e;
	bc[0].static_c[b->number>>1]=c;
	bc[2].static_g[b->number/2*3+1]=g;
	bc[2].static_e[b->number/2*3+1]=e;
	bc[2].static_c[b->number/2*3+1]=c;
      }else{
	if(b->number<19){
	  float val=(bc[2].static_g[b->number/2*3+2]+
		     bc[2].static_g[b->number/2*3+3])*.5;
	  bc[2].static_g[b->number/2*3+2]+=(g-val);
	  bc[2].static_g[b->number/2*3+3]+=(g-val);
	  
	  val=(bc[2].static_e[b->number/2*3+2]+
	       bc[2].static_e[b->number/2*3+3])*.5;
	  bc[2].static_e[b->number/2*3+2]+=(e-val);
	  bc[2].static_e[b->number/2*3+3]+=(e-val);
	  
	  val=(bc[2].static_c[b->number/2*3+2]+
	       bc[2].static_c[b->number/2*3+3])*.5;
	  bc[2].static_c[b->number/2*3+2]+=(c-val);
	  bc[2].static_c[b->number/2*3+3]+=(c-val);
	}else{
	  bc[2].static_g[b->number/2*3+2]=g;
	  bc[2].static_e[b->number/2*3+2]=e;
	  bc[2].static_c[b->number/2*3+2]=c;
	}
      }
      break;
    case 2:
      if((b->number%3)==1){
	bc[0].static_g[b->number/3]=g;
	bc[0].static_e[b->number/3]=e;
	bc[0].static_c[b->number/3]=c;
	bc[1].static_g[b->number/3*2]=g;
	bc[1].static_e[b->number/3*2]=e;
	bc[1].static_c[b->number/3*2]=c;
      }else if(b->number>1){
	if(b->number<29){
	  bc[1].static_g[(b->number-1)/3*2+1]=
	    (bc[2].static_g[(b->number-1)/3*3+2]+
	     bc[2].static_g[(b->number-1)/3*3+3])*.5;
	  bc[1].static_e[(b->number-1)/3*2+1]=
	    (bc[2].static_e[(b->number-1)/3*3+2]+
	     bc[2].static_e[(b->number-1)/3*3+3])*.5;
	  bc[1].static_c[(b->number-1)/3*2+1]=
	    (bc[2].static_c[(b->number-1)/3*3+2]+
	     bc[2].static_c[(b->number-1)/3*3+3])*.5;
	}else{
	  bc[1].static_g[(b->number-1)/3*2+1]=g;
	  bc[1].static_e[(b->number-1)/3*2+1]=e;
	bc[1].static_c[(b->number-1)/3*2+1]=c;
	}
      }
      break;
    }
  }
}

static void static_octave(GtkWidget *w,gpointer in){
  int octave=(int)in,i;

  if(!w || gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))){
    if(bank_active!=octave || w==NULL){
      bank_active=octave;
      
      /* map, unmap, relabel */
      for(i=0;i<multicomp_freqs_max;i++){
	if(i<multicomp_freqs[bank_active]){
	  gtk_label_set_text(GTK_LABEL(bars[i].label),
			     multicomp_freq_labels[bank_active][i]);
	  gtk_widget_show(bars[i].label);
	  gtk_widget_show(bars[i].slider);
	  gtk_widget_show(bars[i].readoutg);
	  gtk_widget_show(bars[i].readoute);
	  gtk_widget_show(bars[i].readoutc);
	  
	  inactive_updatep=0;
	  
	  {
	    float g=bc[bank_active].static_g[i]/10.;
	    float e=bc[bank_active].static_e[i]/10.;
	    float c=bc[bank_active].static_c[i]/10.;

	    multibar_thumb_set(MULTIBAR(bars[i].slider),g,0);
	    multibar_thumb_set(MULTIBAR(bars[i].slider),e,1);
	    multibar_thumb_set(MULTIBAR(bars[i].slider),c,2);
	  }

	  inactive_updatep=1;
	  
	}else{
	  gtk_widget_hide(bars[i].label);
	  gtk_widget_hide(bars[i].slider);
	  gtk_widget_hide(bars[i].readoutg);
	  gtk_widget_hide(bars[i].readoute);
	  gtk_widget_hide(bars[i].readoutc);
	}
      }
      multicompand_set_bank(bank_active);
      
    }
  }
}

static void static_compressor_change(GtkWidget *w,gpointer in){
  char buffer[80];
  Readout *r=READOUT(in);
  float val=-multibar_get_value(MULTIBAR(w),0);

  if(rint(1./val)>=10.)
    sprintf(buffer,"%3.0f:1",1./val);
  else
    sprintf(buffer,"%3.1f:1",1./val);

  if(val==1.)
    sprintf(buffer,"  off");

  readout_set(r,buffer);
  c.static_c_ratio=rint(100./val);
}

static void static_expander_change(GtkWidget *w,gpointer in){
  char buffer[80];
  Readout *r=READOUT(in);
  float val=-multibar_get_value(MULTIBAR(w),0);

  if(rint(1./val)>=10.)
    sprintf(buffer,"%3.0f:1",1./val);
  else
  sprintf(buffer,"%3.1f:1",1./val);

  if(val==1.)
    sprintf(buffer,"  off");

  readout_set(r,buffer);
  c.static_e_ratio=rint(100./val);
}

static void static_decay_change(GtkWidget *w,gpointer in,sig_atomic_t *v){
  char buffer[80];
  Readout *r=READOUT(in);
  float val=multibar_get_value(MULTIBAR(w),0);
  float set=val/input_rate*(1024*1024);
  float ms =-100000./val;

  if(ms<100)
    sprintf(buffer,".%04d",(int)rint(ms*10.));
  else if(ms<1000)
    sprintf(buffer,".%03d",(int)rint(ms));
  else if(ms<10000)
    sprintf(buffer,"%4.2f",ms/1000);
  else
    sprintf(buffer,"%4.1f",ms/1000);

  if(ms<=1)
    sprintf(buffer," fast");

  if(ms>99999.)
    sprintf(buffer," slow");

  readout_set(r,buffer);
  *v=rint(set);
}

static void static_c_decay_change(GtkWidget *w,gpointer in){
  static_decay_change(w,in,&c.static_c_decay);
}
static void static_e_decay_change(GtkWidget *w,gpointer in){
  static_decay_change(w,in,&c.static_e_decay);
}
static void static_g_decay_change(GtkWidget *w,gpointer in){
  static_decay_change(w,in,&c.static_g_decay);
}

static void static_trim_change(GtkWidget *w,gpointer in,sig_atomic_t *v){
  char buffer[80];
  Readout *r=READOUT(in);
  float val=multibar_get_value(MULTIBAR(w),0);

  sprintf(buffer,"%+3.0fdB",val);

  readout_set(r,buffer);
  *v=rint(val*10.);
}

static void static_c_trim_change(GtkWidget *w,gpointer in){
  static_trim_change(w,in,&c.static_c_trim);
}
static void static_e_trim_change(GtkWidget *w,gpointer in){
  static_trim_change(w,in,&c.static_e_trim);
}
static void static_g_trim_change(GtkWidget *w,gpointer in){
  static_trim_change(w,in,&c.static_g_trim);

}

static void static_trim_apply(GtkWidget *w,gpointer in,int gec){
  char buffer[80];
  int i,j;
  Multibar *m=MULTIBAR(in);
  float trim=multibar_get_value(m,0);

  /* apply the trim to all sliders */
  for(j=0;j<multicomp_freqs[bank_active];j++){
    float sv=multibar_get_value(MULTIBAR(bars[j].slider),gec);
    multibar_thumb_set(MULTIBAR(bars[j].slider),sv+trim,gec);
  }

  multibar_thumb_set(m,0,0);
}

static void static_c_trim_apply(GtkWidget *w,gpointer in){
  static_trim_apply(w,in,2);
}
static void static_e_trim_apply(GtkWidget *w,gpointer in){
  static_trim_apply(w,in,1);
}
static void static_g_trim_apply(GtkWidget *w,gpointer in){
  static_trim_apply(w,in,0);
}

static void link_toggled(GtkToggleButton *b,gpointer in){
  int active=gtk_toggle_button_get_active(b);
  c.link_mode=active;
}

static void static_mode(GtkButton *b,gpointer in){
  int mode=(int)in;
  c.static_mode=mode;
}

static void suppressor_mode(GtkButton *b,gpointer in){
  int mode=(int)in;
  c.suppress_mode=mode;
}

static void envelope_mode(GtkButton *b,gpointer in){
  int mode=(int)in;
  c.envelope_mode=mode;
}

static void suppress_decay_change(GtkWidget *w,gpointer in){
  char buffer[80];
  Readout *r=READOUT(in);
  float val=multibar_get_value(MULTIBAR(w),0);
  float set=val/input_rate*(1024*1024);
  float ms =-100000./val;

  if(ms<100)
    sprintf(buffer,".%04d",(int)rint(ms*10.));
  else if(ms<1000)
    sprintf(buffer,".%03d",(int)rint(ms));
  else if(ms<10000)
    sprintf(buffer,"%4.2f",ms/1000);
  else
    sprintf(buffer,"%4.1f",ms/1000);

  if(ms<=1)
    sprintf(buffer," fast");

  if(ms>99999.)
    sprintf(buffer," slow");

  readout_set(r,buffer);
  c.suppress_decay=rint(set);
}

static void suppress_ratio_change(GtkWidget *w,gpointer in){
  char buffer[80];
  Readout *r=READOUT(in);
  float val=-multibar_get_value(MULTIBAR(w),0);

  if(rint(1./val)>=10.)
    sprintf(buffer,"%3.0f:1",1./val);
  else
    sprintf(buffer,"%3.1f:1",1./val);

  if(val==1.)
    sprintf(buffer,"  off");

  readout_set(r,buffer);
  c.suppress_ratio=rint(100./val);
}

static void suppress_depth_change(GtkWidget *w,gpointer in){
  char buffer[80];
  Readout *r=READOUT(in);
  float val=-140.-multibar_get_value(MULTIBAR(w),0);
  
  sprintf(buffer,"%3.0fdB",-val);

  if(val==0.)
    sprintf(buffer,"  off");

  if(val==-140)
    sprintf(buffer," deep");

  readout_set(r,buffer); 
  c.suppress_depth=rint(val*10.);
}

static void envelope_compander_change(GtkWidget *w,gpointer in){
  char buffer[80];
  Readout *r=READOUT(in);
  float val=multibar_get_value(MULTIBAR(w),0);

  sprintf(buffer,"%+5.1f%%",val);
  readout_set(r,buffer);
  c.envelope_c=rint(val*10.);
}

void compandpanel_create(postfish_mainpanel *mp,
			 GtkWidget *windowbutton,
			 GtkWidget *activebutton){
  int i,j;
  char *labels[14]={"130","120","110","100","90","80","70",
		    "60","50","40","30","20","10","0"};
  float levels[15]={-140,-130,-120,-110,-100,-90,-80,-70,-60,-50,-40,
		     -30,-20,-10,0};

  float ratio_levels[7]={-1.,-.67,-.5,-.33,-.25,-.17,-.1};
  char  *ratio_labels[6]={"1.5:1","2:1","3:1","4:1","6:1","10:1"};

  float decay_levels[7]={-100000,-10000,-1000,-100,-10,-3,-1};
  char  *decay_labels[6]={".01",".1","1","10","30","slow"};
    
  float bias_levels[7]={-30,-20,-10,0,10,20,30};
  char  *bias_labels[6]={"20","10","0","10","20","30"};

  float depth_levels[7]={-140,-130,-120,-110,-100,-80,-0};
  char  *depth_labels[6]={"10","20","30","40","60","deep"};


  subpanel_generic *panel=subpanel_create(mp,windowbutton,activebutton,
					  &compand_active,
					  &compand_visible,
					  "_Multiband Compand and Noise Gate"," [m] ");
  
  GtkWidget *hbox=gtk_hbox_new(0,0);
  GtkWidget *sliderbox=gtk_vbox_new(0,0);
  GtkWidget *sliderframe=gtk_frame_new(NULL);
  GtkWidget *staticbox=gtk_vbox_new(0,0);
  GtkWidget *staticframe=gtk_frame_new(NULL);
  GtkWidget *slidertable=gtk_table_new(multicomp_freqs_max+1,5,0);

  GtkWidget *envelopeframe=gtk_frame_new(" Envelope Compander ");
  GtkWidget *suppressframe=gtk_frame_new(" Reverberation Suppressor ");

  GtkWidget *envelopetable=gtk_table_new(3,3,0);
  GtkWidget *suppresstable=gtk_table_new(4,3,0);
  GtkWidget *statictable=gtk_table_new(10,4,0);

  GtkWidget *link_box=gtk_hbox_new(0,0);
  GtkWidget *link_check=gtk_check_button_new_with_mnemonic("_link channel envelopes");

  {
  
    GtkWidget *octave_box=gtk_hbox_new(0,0);
    GtkWidget *octave_a=gtk_radio_button_new_with_label(NULL,"full-octave");
    GtkWidget *octave_b=
      gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(octave_a),
						  "half-octave");
    GtkWidget *octave_c=
      gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(octave_b),
						  "third-octave");
    GtkWidget *label2=gtk_label_new("gate");
    GtkWidget *label3=gtk_label_new("expd");
    GtkWidget *label4=gtk_label_new("cmpr");

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(octave_c),1);

    g_signal_connect (G_OBJECT (octave_a), "clicked",
		      G_CALLBACK (static_octave), (gpointer)0);
    g_signal_connect (G_OBJECT (octave_b), "clicked",
		      G_CALLBACK (static_octave), (gpointer)1);
    g_signal_connect (G_OBJECT (octave_c), "clicked",
		      G_CALLBACK (static_octave), (gpointer)2);
   
    gtk_table_attach(GTK_TABLE(slidertable),label2,2,3,0,1,GTK_FILL,0,2,0);
    gtk_table_attach(GTK_TABLE(slidertable),label3,3,4,0,1,GTK_FILL,0,2,0);
    gtk_table_attach(GTK_TABLE(slidertable),label4,4,5,0,1,GTK_FILL,0,2,0);

    gtk_box_pack_start(GTK_BOX(octave_box),octave_a,0,1,3);
    gtk_box_pack_start(GTK_BOX(octave_box),octave_b,0,1,3);
    gtk_box_pack_start(GTK_BOX(octave_box),octave_c,0,1,3);
    gtk_table_attach(GTK_TABLE(slidertable),octave_box,1,2,0,1,
		     GTK_EXPAND,0,0,0);

    gtk_box_pack_start(GTK_BOX(sliderbox),sliderframe,0,0,0);
    gtk_container_add(GTK_CONTAINER(sliderframe),slidertable);

    gtk_frame_set_shadow_type(GTK_FRAME(sliderframe),GTK_SHADOW_NONE);
    
    gtk_container_set_border_width(GTK_CONTAINER(sliderframe),4);

  }

  if(input_ch>1)
    gtk_box_pack_end(GTK_BOX(link_box),link_check,0,0,0);  

  gtk_box_pack_end(GTK_BOX(staticbox),link_box,0,0,0);
  gtk_container_set_border_width(GTK_CONTAINER(link_box),5);
  g_signal_connect (G_OBJECT (link_check), "toggled",
		    G_CALLBACK (link_toggled), (gpointer)0);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(link_check),0);

  gtk_box_pack_start(GTK_BOX(panel->subpanel_box),hbox,0,0,0);
  gtk_box_pack_start(GTK_BOX(hbox),sliderbox,0,0,0);
  gtk_box_pack_start(GTK_BOX(hbox),staticbox,0,0,0);

  gtk_box_pack_start(GTK_BOX(staticbox),staticframe,0,0,0);
  gtk_box_pack_end(GTK_BOX(staticbox),suppressframe,0,0,0);
  gtk_box_pack_end(GTK_BOX(staticbox),envelopeframe,0,0,0);
  gtk_container_add(GTK_CONTAINER(staticframe),statictable);
  gtk_container_add(GTK_CONTAINER(envelopeframe),envelopetable);
  gtk_container_add(GTK_CONTAINER(suppressframe),suppresstable);

  gtk_container_set_border_width(GTK_CONTAINER(suppressframe),5);
  gtk_container_set_border_width(GTK_CONTAINER(envelopeframe),5);
  gtk_container_set_border_width(GTK_CONTAINER(envelopeframe),5);
  gtk_frame_set_shadow_type(GTK_FRAME(staticframe),GTK_SHADOW_NONE);

  /* static compand */
  {

    //GtkWidget *label0=gtk_label_new("mode:");
    GtkWidget *envelopebox=gtk_hbox_new(0,0);
    GtkWidget *rms_button=gtk_radio_button_new_with_label(NULL,"RMS");
    GtkWidget *peak_button=
      gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(rms_button),
						  "peak");

    GtkWidget *labelR=gtk_label_new("Ratio");
    GtkWidget *labelR1=gtk_label_new("cmpr:");
    GtkWidget *labelR2=gtk_label_new("expd:");
    GtkWidget *readoutR1=readout_new("1.5:1");
    GtkWidget *readoutR2=readout_new("1.5:1");
    GtkWidget *sliderR1=multibar_slider_new(6,ratio_labels,ratio_levels,1);
    GtkWidget *sliderR2=multibar_slider_new(6,ratio_labels,ratio_levels,1);

    GtkWidget *labelD=gtk_label_new("Decay");
    GtkWidget *labelD1=gtk_label_new("cmpr:");
    GtkWidget *labelD2=gtk_label_new("expd:");
    GtkWidget *labelD3=gtk_label_new("gate:");
    GtkWidget *readoutD1=readout_new(" fast");
    GtkWidget *readoutD2=readout_new(" fast");
    GtkWidget *readoutD3=readout_new(" fast");
    GtkWidget *sliderD1=multibar_slider_new(6,decay_labels,decay_levels,1);
    GtkWidget *sliderD2=multibar_slider_new(6,decay_labels,decay_levels,1);
    GtkWidget *sliderD3=multibar_slider_new(6,decay_labels,decay_levels,1);

    GtkWidget *labelT=gtk_label_new("Trim");
    GtkWidget *labelT1=gtk_label_new("cmpr:");
    GtkWidget *labelT2=gtk_label_new("expd:");
    GtkWidget *labelT3=gtk_label_new("gate:");
    GtkWidget *readoutT1=readout_new("-40dB");
    GtkWidget *readoutT2=readout_new("-40dB");
    GtkWidget *readoutT3=readout_new("-40dB");
    GtkWidget *sliderT1=multibar_slider_new(6,bias_labels,bias_levels,1);
    GtkWidget *sliderT2=multibar_slider_new(6,bias_labels,bias_levels,1);
    GtkWidget *sliderT3=multibar_slider_new(6,bias_labels,bias_levels,1);
    GtkWidget *buttonT1=gtk_button_new_with_label("apply");
    GtkWidget *buttonT2=gtk_button_new_with_label("apply");
    GtkWidget *buttonT3=gtk_button_new_with_label("apply");

    gtk_box_pack_end(GTK_BOX(envelopebox),peak_button,0,0,5);
    gtk_box_pack_end(GTK_BOX(envelopebox),rms_button,0,0,5);
    g_signal_connect (G_OBJECT (rms_button), "clicked",
		      G_CALLBACK (static_mode), (gpointer)0); //To Hell I Go
    g_signal_connect (G_OBJECT (peak_button), "clicked",
		      G_CALLBACK (static_mode), (gpointer)1);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rms_button),1);

    g_signal_connect (G_OBJECT (buttonT1), "clicked",
		      G_CALLBACK (static_c_trim_apply), sliderT1);
    g_signal_connect (G_OBJECT (buttonT2), "clicked",
		      G_CALLBACK (static_e_trim_apply), sliderT2);
    g_signal_connect (G_OBJECT (buttonT3), "clicked",
		      G_CALLBACK (static_g_trim_apply), sliderT3);

    gtk_container_set_border_width(GTK_CONTAINER(statictable),4);

    multibar_callback(MULTIBAR(sliderR1),static_compressor_change,readoutR1);
    multibar_thumb_set(MULTIBAR(sliderR1),-1.,0);
    multibar_callback(MULTIBAR(sliderR2),static_expander_change,readoutR2);
    multibar_thumb_set(MULTIBAR(sliderR2),-1.,0);

    multibar_callback(MULTIBAR(sliderD1),static_c_decay_change,readoutD1);
    multibar_thumb_set(MULTIBAR(sliderD1),-100000.,0);
    multibar_callback(MULTIBAR(sliderD2),static_e_decay_change,readoutD2);
    multibar_thumb_set(MULTIBAR(sliderD2),-100.,0);
    multibar_callback(MULTIBAR(sliderD3),static_g_decay_change,readoutD3);
    multibar_thumb_set(MULTIBAR(sliderD3),-10.,0);

    multibar_callback(MULTIBAR(sliderT1),static_c_trim_change,readoutT1);
    multibar_thumb_increment(MULTIBAR(sliderT1),1.,10.);
    multibar_callback(MULTIBAR(sliderT2),static_e_trim_change,readoutT2);
    multibar_thumb_increment(MULTIBAR(sliderT2),1.,10.);
    multibar_callback(MULTIBAR(sliderT3),static_g_trim_change,readoutT3);
    multibar_thumb_increment(MULTIBAR(sliderT3),1.,10.);

    //gtk_misc_set_alignment(GTK_MISC(labelR),0,.5);
    //gtk_misc_set_alignment(GTK_MISC(labelD),0,.5);
    //gtk_misc_set_alignment(GTK_MISC(labelT),0,.5);
    gtk_widget_set_name(labelR,"framelabel");
    gtk_widget_set_name(labelD,"framelabel");
    gtk_widget_set_name(labelT,"framelabel");

    gtk_misc_set_alignment(GTK_MISC(labelR1),1,.5);
    gtk_misc_set_alignment(GTK_MISC(labelR2),1,.5);
    gtk_misc_set_alignment(GTK_MISC(labelD1),1,.5);
    gtk_misc_set_alignment(GTK_MISC(labelD2),1,.5);
    gtk_misc_set_alignment(GTK_MISC(labelD3),1,.5);
    gtk_misc_set_alignment(GTK_MISC(labelT1),1,.5);
    gtk_misc_set_alignment(GTK_MISC(labelT2),1,.5);
    gtk_misc_set_alignment(GTK_MISC(labelT3),1,.5);

    //gtk_table_attach(GTK_TABLE(statictable),label0,0,1,0,1,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(statictable),envelopebox,1,4,0,1,GTK_FILL,0,0,0);


    gtk_table_attach(GTK_TABLE(statictable),labelR,0,4,0,1,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(statictable),labelR1,0,1,1,2,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(statictable),labelR2,0,1,2,3,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(statictable),sliderR1,1,3,1,2,
		     GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(statictable),sliderR2,1,3,2,3,
		     GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(statictable),readoutR1,3,4,1,2,GTK_FILL,0,0,2);
    gtk_table_attach(GTK_TABLE(statictable),readoutR2,3,4,2,3,GTK_FILL,0,0,2);


    gtk_table_attach(GTK_TABLE(statictable),labelD,0,4,3,4,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(statictable),labelD1,0,1,4,5,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(statictable),labelD2,0,1,5,6,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(statictable),labelD3,0,1,6,7,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(statictable),sliderD1,1,3,4,5,
		     GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(statictable),sliderD2,1,3,5,6,
		     GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(statictable),sliderD3,1,3,6,7,
		     GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(statictable),readoutD1,3,4,4,5,GTK_FILL,0,0,2);
    gtk_table_attach(GTK_TABLE(statictable),readoutD2,3,4,5,6,GTK_FILL,0,0,2);
    gtk_table_attach(GTK_TABLE(statictable),readoutD3,3,4,6,7,GTK_FILL,0,0,2);

    gtk_table_attach(GTK_TABLE(statictable),labelT,0,4,7,8,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(statictable),labelT1,0,1,8,9,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(statictable),labelT2,0,1,9,10,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(statictable),labelT3,0,1,10,11,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(statictable),sliderT1,1,2,8,9,
		     GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(statictable),sliderT2,1,2,9,10,
		     GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(statictable),sliderT3,1,2,10,11,
		     GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(statictable),buttonT1,3,4,8,9,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(statictable),buttonT2,3,4,9,10,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(statictable),buttonT3,3,4,10,11,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(statictable),readoutT1,2,3,8,9,GTK_FILL,0,0,2);
    gtk_table_attach(GTK_TABLE(statictable),readoutT2,2,3,9,10,GTK_FILL,0,0,2);
    gtk_table_attach(GTK_TABLE(statictable),readoutT3,2,3,10,11,GTK_FILL,0,0,2);

    gtk_table_set_row_spacing(GTK_TABLE(statictable),2,5);
    gtk_table_set_row_spacing(GTK_TABLE(statictable),6,5);
    gtk_table_set_row_spacing(GTK_TABLE(statictable),10,10);

    gtk_container_set_border_width(GTK_CONTAINER(statictable),4);
 }

  /* envelope compand */
  {
    float levels[11]={-50,-40,-30,-20,-10,0,10,20,30,40,50};
    char *labels[10]={"40%","30%","20%","10%","0%","10%","20%",
		      "30%","40%","50%"};

    //GtkWidget *label0=gtk_label_new("mode:");
    GtkWidget *label1=gtk_label_new("compress");
    GtkWidget *label2=gtk_label_new("expand");
    GtkWidget *envelope_compress_readout=readout_new(" 0.0%");
    GtkWidget *envelope_compress_slider=multibar_slider_new(10,labels,levels,1);

    GtkWidget *envelopebox=gtk_hbox_new(0,0);
    GtkWidget *rms_button=gtk_radio_button_new_with_label(NULL,"RMS");
    GtkWidget *peak_button=
      gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(rms_button),
						  "peak");

    //gtk_box_pack_start(GTK_BOX(envelopebox),label0,0,0,1);
    gtk_box_pack_end(GTK_BOX(envelopebox),peak_button,0,0,5);
    gtk_box_pack_end(GTK_BOX(envelopebox),rms_button,0,0,5);

    g_signal_connect (G_OBJECT (rms_button), "clicked",
		      G_CALLBACK (envelope_mode), (gpointer)0); //To Hell I Go
    g_signal_connect (G_OBJECT (peak_button), "clicked",
		      G_CALLBACK (envelope_mode), (gpointer)1);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rms_button),1);

   
    gtk_container_set_border_width(GTK_CONTAINER(envelopetable),4);
    multibar_callback(MULTIBAR(envelope_compress_slider),
		      envelope_compander_change,
		      envelope_compress_readout);
    multibar_thumb_increment(MULTIBAR(envelope_compress_slider),.1,1.);

    gtk_widget_set_name(label1,"smallmarker");
    gtk_widget_set_name(label2,"smallmarker");
    gtk_misc_set_alignment(GTK_MISC(label1),0,.5);
    gtk_misc_set_alignment(GTK_MISC(label2),1,.5);

    gtk_table_attach(GTK_TABLE(envelopetable),label1,0,1,2,3,GTK_FILL,0,2,0);
    gtk_table_attach(GTK_TABLE(envelopetable),label2,1,2,2,3,GTK_FILL,0,2,0);
    gtk_table_attach(GTK_TABLE(envelopetable),envelopebox,
		     0,3,0,1,GTK_FILL,0,0,0);

    gtk_table_attach(GTK_TABLE(envelopetable),envelope_compress_slider,
		     0,2,1,2,GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(envelopetable),envelope_compress_readout,
		     2,3,1,2,GTK_FILL,0,0,0);

  }

  /* suppressor */
  {

    GtkWidget *suppress_decay_slider=multibar_slider_new(6,decay_labels,decay_levels,1);
    GtkWidget *suppress_ratio_slider=multibar_slider_new(6,ratio_labels,ratio_levels,1);
    GtkWidget *suppress_depth_slider=multibar_slider_new(6,depth_labels,depth_levels,1);
    GtkWidget *suppress_decay_readout=readout_new("fast");
    GtkWidget *suppress_ratio_readout=readout_new("1.5:1");
    GtkWidget *suppress_depth_readout=readout_new("gate");

    //GtkWidget *label1=gtk_label_new("mode:");
    GtkWidget *label3=gtk_label_new("decay:");
    GtkWidget *label4=gtk_label_new("ratio:");
    GtkWidget *label5=gtk_label_new("depth:");
    
    GtkWidget *envelopebox=gtk_hbox_new(0,0);
    GtkWidget *suppress_rms_button=gtk_radio_button_new_with_label(NULL,"RMS");
    GtkWidget *suppress_peak_button=
      gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(suppress_rms_button),
						  "peak");
    gtk_box_pack_end(GTK_BOX(envelopebox),suppress_peak_button,0,0,5);
    gtk_box_pack_end(GTK_BOX(envelopebox),suppress_rms_button,0,0,5);


    g_signal_connect (G_OBJECT (suppress_rms_button), "clicked",
		      G_CALLBACK (suppressor_mode), (gpointer)0); //To Hell I Go
    g_signal_connect (G_OBJECT (suppress_peak_button), "clicked",
		      G_CALLBACK (suppressor_mode), (gpointer)1);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(suppress_rms_button),1);


    gtk_container_set_border_width(GTK_CONTAINER(suppresstable),4);

    //gtk_misc_set_alignment(GTK_MISC(label1),1,.5);
    gtk_misc_set_alignment(GTK_MISC(label3),1,.5);
    gtk_misc_set_alignment(GTK_MISC(label4),1,.5);
    gtk_misc_set_alignment(GTK_MISC(label5),1,.5);


    //gtk_table_attach(GTK_TABLE(suppresstable),label1,0,1,0,1,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(suppresstable),label3,0,1,1,2,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(suppresstable),label4,0,1,2,3,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(suppresstable),label5,0,1,3,4,GTK_FILL,0,0,0);

    gtk_table_attach(GTK_TABLE(suppresstable),envelopebox,1,3,0,1,GTK_FILL,0,0,0);

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
		     suppress_decay_slider,1,2,1,2,GTK_FILL|GTK_EXPAND,
		     GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(suppresstable),
		     suppress_ratio_slider,1,2,2,3,GTK_FILL|GTK_EXPAND,
		     GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(suppresstable),
		     suppress_depth_slider,1,2,3,4,GTK_FILL|GTK_EXPAND,
		     GTK_FILL|GTK_EXPAND,2,0);

    gtk_table_attach(GTK_TABLE(suppresstable),
		     suppress_decay_readout,2,3,1,2,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(suppresstable),
		     suppress_ratio_readout,2,3,2,3,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(suppresstable),
		     suppress_depth_readout,2,3,3,4,GTK_FILL,0,0,0);

  }

  for(i=0;i<multicomp_freqs_max;i++){
    GtkWidget *label=gtk_label_new(NULL);
    gtk_widget_set_name(label,"smallmarker");
    
    bars[i].readoutg=readout_new("  +0");
    bars[i].readoute=readout_new("  +0");
    bars[i].readoutc=readout_new("  +0");
    bars[i].slider=multibar_new(14,labels,levels,3,
				LO_DECAY|HI_DECAY|LO_ATTACK);
    bars[i].number=i;
    bars[i].label=label;

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
		     0,0,0,0);
    gtk_table_attach(GTK_TABLE(slidertable),bars[i].readoute,3,4,i+1,i+2,
		       0,0,0,0);
      gtk_table_attach(GTK_TABLE(slidertable),bars[i].readoutc,4,5,i+1,i+2,
		       0,0,0,0);
      gtk_table_attach(GTK_TABLE(slidertable),bars[i].slider,1,2,i+1,i+2,
		       GTK_FILL|GTK_EXPAND,GTK_EXPAND,0,0);
  }

  subpanel_show_all_but_toplevel(panel);

  /* Now unmap the sliders we don't want */
  static_octave(NULL,(gpointer)2);

}

static float **peakfeed=0;
static float **rmsfeed=0;

void compandpanel_feedback(int displayit){
  int i,bands;
  if(!peakfeed){
    peakfeed=malloc(sizeof(*peakfeed)*multicomp_freqs_max);
    rmsfeed=malloc(sizeof(*rmsfeed)*multicomp_freqs_max);

    for(i=0;i<multicomp_freqs_max;i++){
      peakfeed[i]=malloc(sizeof(**peakfeed)*input_ch);
      rmsfeed[i]=malloc(sizeof(**rmsfeed)*input_ch);
    }
  }

  if(pull_multicompand_feedback(peakfeed,rmsfeed,&bands)==1)
    for(i=0;i<bands;i++)
      multibar_set(MULTIBAR(bars[i].slider),rmsfeed[i],peakfeed[i],
		   input_ch,(displayit && compand_visible));
}

void compandpanel_reset(void){
  int i,j;
  for(i=0;i<multicomp_freqs_max;i++)
    multibar_reset(MULTIBAR(bars[i].slider));
}


