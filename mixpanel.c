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
#include "mix.h"
#include "config.h"

typedef struct {
  GtkWidget *s;
  GtkWidget *r;
  sig_atomic_t *val;
} slider_readout_pair;

/* only the sliders we need to save for feedback */
typedef struct {
  subpanel_generic *panel;
  slider_readout_pair **att;
  slider_readout_pair **del;

  GtkWidget **master;

  GtkWidget *average;
} atten_panelsave;

typedef struct {
  GtkWidget *insert_source[MIX_BLOCKS][3];
  GtkWidget *insert_invert[MIX_BLOCKS];
  slider_readout_pair *insert_att[MIX_BLOCKS];
  slider_readout_pair *insert_del[MIX_BLOCKS];
  GtkWidget *insert_dest[MIX_BLOCKS][OUTPUT_CHANNELS];
  
  GtkWidget *destA[OUTPUT_CHANNELS];
  GtkWidget *destB[OUTPUT_CHANNELS];
  slider_readout_pair *place_AB;
  slider_readout_pair *place_atten;
  slider_readout_pair *place_delay;

  GtkWidget *place[2];
  GtkWidget *sub[MIX_BLOCKS];
} mix_panelsave;

static atten_panelsave atten_panel;
static mix_panelsave **mix_panels;

static void mixblock_state_to_config(int bank, mix_settings *s,int A,int B){
  config_set_vector("mixblock_source",bank,A,B,0,3,s->insert_source[B]);
  config_set_integer("mixblock_set",bank,A,B,0,0,s->insert_invert[B]);
  config_set_integer("mixblock_set",bank,A,B,0,1,s->insert_att[B]);
  config_set_integer("mixblock_set",bank,A,B,0,2,s->insert_delay[B]);
  config_set_vector("mixblock_dest",bank,A,B,0,OUTPUT_CHANNELS,s->insert_dest[B]);
}

static void mixdown_state_to_config(int bank, mix_settings *s,int A){
  int i;
  config_set_vector("mixplace_dest",bank,A,0,0,OUTPUT_CHANNELS,s->placer_destA);
  config_set_vector("mixplace_dest",bank,A,1,0,OUTPUT_CHANNELS,s->placer_destB);
  config_set_integer("mixplace_set",bank,A,0,0,0,s->placer_place);
  config_set_integer("mixplace_set",bank,A,0,0,1,s->placer_att);
  config_set_integer("mixplace_set",bank,A,0,0,2,s->placer_delay);
  for(i=0;i<MIX_BLOCKS;i++)
    mixblock_state_to_config(bank,s,A,i);
}

void mixpanel_state_to_config(int bank){
  int i;
  config_set_vector("mixdown_active",bank,0,0,0,input_ch,mixpanel_active);

  for(i=0;i<input_ch;i++){
    config_set_integer("mixdown_master_attenuate",bank,0,0,0,i,mix_set[i].master_att);
    config_set_integer("mixdown_master_delay",bank,0,0,0,i,mix_set[i].master_delay);

    mixdown_state_to_config(bank,mix_set+i,i);
  }
}

static void mixblock_state_from_config(int bank, mix_settings *s,mix_panelsave *p,int A,int B){
  int i;
  config_get_vector("mixblock_source",bank,A,B,0,3,s->insert_source[B]);
  for(i=0;i<3;i++)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->insert_source[B][i]),
				 s->insert_source[B][i]);

  config_get_sigat("mixblock_set",bank,A,B,0,0,&s->insert_invert[B]);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->insert_invert[B]),
			       s->insert_invert[B]);
  
  config_get_sigat("mixblock_set",bank,A,B,0,1,&s->insert_att[B]);
  multibar_thumb_set(MULTIBAR(p->insert_att[B]->s),s->insert_att[B]*.1,0);
  
  config_get_sigat("mixblock_set",bank,A,B,0,2,&s->insert_delay[B]);
  multibar_thumb_set(MULTIBAR(p->insert_del[B]->s),s->insert_delay[B]*.01,0);
  
  config_get_vector("mixblock_dest",bank,A,B,0,OUTPUT_CHANNELS,s->insert_dest[B]);
  for(i=0;i<OUTPUT_CHANNELS;i++)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->insert_dest[B][i]),
				 s->insert_dest[B][i]);
}

static void mixdown_state_from_config(int bank, mix_settings *s, mix_panelsave *p, int A){
  int i;
  config_get_vector("mixplace_dest",bank,A,0,0,OUTPUT_CHANNELS,s->placer_destA);
  config_get_vector("mixplace_dest",bank,A,1,0,OUTPUT_CHANNELS,s->placer_destB);
  config_get_sigat("mixplace_set",bank,A,0,0,0,&s->placer_place);
  config_get_sigat("mixplace_set",bank,A,0,0,1,&s->placer_att);
  config_get_sigat("mixplace_set",bank,A,0,0,2,&s->placer_delay);

  for(i=0;i<OUTPUT_CHANNELS;i++){
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->destA[i]),s->placer_destA[i]);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->destB[i]),s->placer_destB[i]);
  }
  multibar_thumb_set(MULTIBAR(p->place_AB->s),s->placer_place,0);
  multibar_thumb_set(MULTIBAR(p->place_atten->s),s->placer_att*.1,0);
  multibar_thumb_set(MULTIBAR(p->place_delay->s),s->placer_delay*.01,0);

  for(i=0;i<MIX_BLOCKS;i++)
    mixblock_state_from_config(bank,s,p,A,i);
}

void mixpanel_state_from_config(int bank){
  int i;
  config_get_vector("mixdown_active",bank,0,0,0,input_ch,mixpanel_active);

  for(i=0;i<input_ch;i++){
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(atten_panel.panel->subpanel_activebutton[i]),
						   mixpanel_active[i]);

    config_get_sigat("mixdown_master_attenuate",bank,0,0,0,i,&mix_set[i].master_att);
    config_get_sigat("mixdown_master_delay",bank,0,0,0,i,&mix_set[i].master_delay);

    multibar_thumb_set(MULTIBAR(atten_panel.att[i]->s),mix_set[i].master_att*.1,0);
    multibar_thumb_set(MULTIBAR(atten_panel.del[i]->s),mix_set[i].master_delay*.01,0);

    mixdown_state_from_config(bank,mix_set+i,mix_panels[i],i);
  }
}


static int av_callback_enter=1;

static float determine_average(void){
  int i;
  float acc=0;
  for(i=0;i<input_ch;i++)
    acc+=multibar_get_value(MULTIBAR(atten_panel.att[i]->s),0);
  return acc/input_ch;
}

static void av_slider_change(GtkWidget *w,gpointer in){
  if(!av_callback_enter){
    char buffer[80];
    atten_panelsave *p=(atten_panelsave *)in;
    
    float av=multibar_get_value(MULTIBAR(p->average),0);
    float actual=determine_average();
    int i;

    av_callback_enter=1;
    for(i=0;i<input_ch;i++){
      float val=multibar_get_value(MULTIBAR(atten_panel.att[i]->s),0) + av - actual;
      multibar_thumb_set(MULTIBAR(atten_panel.att[i]->s),val,0);
    }
    
    av_callback_enter=0;
  }
}

static void dB_slider_change(GtkWidget *w,gpointer in){
  char buffer[80];
  slider_readout_pair *p=(slider_readout_pair *)in;
  float val=multibar_get_value(MULTIBAR(p->s),0);
  
  sprintf(buffer,"%+4.1fdB",val);
  readout_set(READOUT(p->r),buffer);
  
  *p->val=rint(val*10);

  if(!av_callback_enter){
    av_callback_enter=1;
    float actual=determine_average();
    multibar_thumb_set(MULTIBAR(atten_panel.average),actual,0);
    av_callback_enter=0;
  }
}

static void ms_slider_change(GtkWidget *w,gpointer in){
  char buffer[80];
  slider_readout_pair *p=(slider_readout_pair *)in;
  float val=multibar_get_value(MULTIBAR(p->s),0);

  if(val>-1. && val<1.){
    sprintf(buffer,"%+4.2fms",val);
  }else{
    sprintf(buffer,"%+4.1fms",val);
  }
  readout_set(READOUT(p->r),buffer);  
  *p->val=rint(val*100);
}

static void AB_slider_change(GtkWidget *w,gpointer in){
  char buffer[80];
  slider_readout_pair *p=(slider_readout_pair *)in;
  float val=multibar_get_value(MULTIBAR(p->s),0);
  
  if(val==100){
    sprintf(buffer,"center");
  }else if(val<100){
    sprintf(buffer,"A+%d%%",(int)(100-val));
  }else{
    sprintf(buffer,"B+%d%%",(int)(val-100));
  }
  readout_set(READOUT(p->r),buffer);
  
  *p->val=rint(val);
}

static void toggle_callback(GtkWidget *w,gpointer in){
  GtkToggleButton *b=GTK_TOGGLE_BUTTON(w);
  sig_atomic_t *val=(sig_atomic_t *)in;
  
  *val=gtk_toggle_button_get_active(b);
}

static char *labels_dB[11]={""," -60","-40","-20","-10","0","10","20","40","60","80"};
static float levels_dB[11]={-80,-60,-40,-20,-10,0,10,20,40,60,80};

static char *labels_dBn[6]={"","-40","-20","-10","0","+10"};
static float levels_dBn[6]={-80,-40,-20,-10,0,10};
  
static char *labels_del[6]={"","-20","-10","-5","-1","-0"};
static float levels_del[6]={-50,-20,-10,-5,-1,0};


static mix_panelsave *mixpanel_create_helper(postfish_mainpanel *mp,
					     subpanel_generic *panel,
					     mix_settings *m,
					     int thisch){

  int i,j;

  char *labels_dBnn[6]={"","-40","-20","-10","-3","0"};
  float levels_dBnn[6]={-80,-40,-20,-10,-3,0};
  
  char *labels_AB[3]={"A","center","B"};
  float levels_AB[3]={0,100,200};

  GtkWidget *table=gtk_table_new(12+MIX_BLOCKS*3,6,0);
  mix_panelsave *ps=calloc(1,sizeof(*ps));

  /* crossplace marker */
  {
    GtkWidget *box=gtk_hbox_new(0,0);
    GtkWidget *l=gtk_label_new("Crossplace ");
    GtkWidget *h=gtk_hseparator_new();

    gtk_widget_set_name(l,"framelabel");

    gtk_box_pack_start(GTK_BOX(box),l,0,0,0);
    gtk_box_pack_start(GTK_BOX(box),h,1,1,0);
    gtk_table_attach(GTK_TABLE(table),box,0,6,2,3,
		     GTK_FILL,0,0,2);
    gtk_table_set_row_spacing(GTK_TABLE(table),1,10);
  }

  /* crossplace controls */
  {
    slider_readout_pair *AB=calloc(1,sizeof(*AB));
    slider_readout_pair *att=calloc(1,sizeof(*att));
    slider_readout_pair *del=calloc(1,sizeof(*del));
    GtkWidget *boxA=gtk_hbox_new(1,0);
    GtkWidget *boxB=gtk_hbox_new(1,0);

    GtkWidget *lA=gtk_label_new("output A");
    GtkWidget *lB=gtk_label_new("output B");

    GtkWidget *latt=gtk_label_new("crossatten ");
    GtkWidget *ldel=gtk_label_new("crossdelay ");
    gtk_misc_set_alignment(GTK_MISC(latt),1,.5);
    gtk_misc_set_alignment(GTK_MISC(ldel),1,.5);

    AB->s=multibar_slider_new(3,labels_AB,levels_AB,1);
    att->s=multibar_slider_new(6,labels_dBnn,levels_dBnn,1);
    del->s=multibar_slider_new(6,labels_del,levels_del,1);
    AB->r=readout_new("A+000");
    att->r=readout_new("+00.0dB");
    del->r=readout_new("+00.0ms");
    AB->val=&m->placer_place;
    att->val=&m->placer_att;
    del->val=&m->placer_delay;

    ps->place_AB=AB;
    ps->place_atten=att;
    ps->place_delay=del;

    multibar_callback(MULTIBAR(AB->s),AB_slider_change,AB);
    multibar_thumb_set(MULTIBAR(AB->s),100,0);
    multibar_callback(MULTIBAR(att->s),dB_slider_change,att);
    multibar_thumb_set(MULTIBAR(att->s),-3,0);
    multibar_callback(MULTIBAR(del->s),ms_slider_change,del);
    multibar_thumb_set(MULTIBAR(del->s),-1,0);

    ps->place[0]=multibar_new(6,labels_dBn,levels_dBn,0,
			      LO_ATTACK|LO_DECAY|HI_DECAY);
    ps->place[1]=multibar_new(6,labels_dBn,levels_dBn,0,
			      LO_ATTACK|LO_DECAY|HI_DECAY);

    for(i=0;i<OUTPUT_CHANNELS;i++){
      char buffer[80];
      GtkWidget *bA,*bB;
      
      sprintf(buffer," %d ",i+1);

      bA=gtk_toggle_button_new_with_label(buffer);
      bB=gtk_toggle_button_new_with_label(buffer);

      gtk_box_pack_start(GTK_BOX(boxA),bA,1,1,0);
      gtk_box_pack_start(GTK_BOX(boxB),bB,1,1,0);

      g_signal_connect (G_OBJECT (bA), "clicked",
			G_CALLBACK (toggle_callback), 
			(gpointer)&m->placer_destA[i]);
      g_signal_connect (G_OBJECT (bB), "clicked",
			G_CALLBACK (toggle_callback), 
			(gpointer)&m->placer_destB[i]);

      ps->destA[i]=bA;
      ps->destB[i]=bB;
    }
    
    gtk_table_attach(GTK_TABLE(table),lA,0,2,6,7,
		     0,0,0,0);
    gtk_table_attach(GTK_TABLE(table),lB,5,6,6,7,
		     0,0,0,0);

    gtk_table_attach(GTK_TABLE(table),ps->place[0],0,2,4,5,
		     GTK_FILL|GTK_EXPAND,0,0,0);
    gtk_table_attach(GTK_TABLE(table),ps->place[1],5,6,4,5,
		     GTK_FILL|GTK_EXPAND,0,0,0);

    gtk_table_attach(GTK_TABLE(table),boxA,0,2,5,6,
		     GTK_FILL|GTK_EXPAND,0,0,0);
    gtk_table_attach(GTK_TABLE(table),boxB,5,6,5,6,
		     GTK_FILL|GTK_EXPAND,0,0,0);
    gtk_table_attach(GTK_TABLE(table),AB->s,2,4,4,5,
		     GTK_FILL|GTK_EXPAND,0,2,0);
    gtk_table_attach(GTK_TABLE(table),att->s,3,4,5,6,
		     GTK_FILL|GTK_EXPAND,0,2,0);
    gtk_table_attach(GTK_TABLE(table),del->s,3,4,6,7,
		     GTK_FILL|GTK_EXPAND,0,2,0);
    gtk_table_attach(GTK_TABLE(table),AB->r,4,5,4,5,
		     GTK_FILL|GTK_EXPAND,0,0,0);
    gtk_table_attach(GTK_TABLE(table),att->r,4,5,5,6,
		     GTK_FILL|GTK_EXPAND,0,0,0);
    gtk_table_attach(GTK_TABLE(table),del->r,4,5,6,7,
		     GTK_FILL|GTK_EXPAND,0,0,0);

    gtk_table_attach(GTK_TABLE(table),latt,2,3,5,6,
		     GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(table),ldel,2,3,6,7,
		     GTK_FILL,0,0,0);


  }

  /* Direct Mix marker */
  {
    GtkWidget *box=gtk_hbox_new(0,0);
    GtkWidget *l=gtk_label_new("Direct Mixdown Blocks ");
    GtkWidget *h=gtk_hseparator_new();

    GtkWidget *ls=gtk_label_new("source");
    GtkWidget *lo=gtk_label_new("output");

    gtk_widget_set_name(l,"framelabel");

    gtk_box_pack_start(GTK_BOX(box),l,0,0,0);
    gtk_box_pack_start(GTK_BOX(box),h,1,1,0);

    gtk_table_attach(GTK_TABLE(table),box,0,6,7,8,
		     GTK_FILL,0,0,2);
    gtk_table_attach(GTK_TABLE(table),ls,0,2,11+MIX_BLOCKS*3,12+MIX_BLOCKS*3,
		     GTK_FILL,0,0,2);
    gtk_table_attach(GTK_TABLE(table),lo,5,6,11+MIX_BLOCKS*3,12+MIX_BLOCKS*3,
		     GTK_FILL,0,0,2);
    gtk_table_set_row_spacing(GTK_TABLE(table),6,10);

  }

  for(i=0;i<MIX_BLOCKS;i++){
    slider_readout_pair *att=calloc(1,sizeof(*att));
    slider_readout_pair *del=calloc(1,sizeof(*del));

    GtkWidget *boxA=gtk_hbox_new(0,0);
    GtkWidget *boxB=gtk_hbox_new(1,0);

    GtkWidget *bI=gtk_check_button_new_with_mnemonic("_invert source");

    GtkWidget *bM=gtk_toggle_button_new_with_label("master");
    GtkWidget *bA=gtk_toggle_button_new_with_label("revA");
    GtkWidget *bB=gtk_toggle_button_new_with_label("revB");
    GtkWidget *h=gtk_hseparator_new();

    GtkWidget *latt=gtk_label_new("  attenuation ");
    GtkWidget *ldel=gtk_label_new("  delay ");
    gtk_misc_set_alignment(GTK_MISC(latt),1,.5);
    gtk_misc_set_alignment(GTK_MISC(ldel),1,.5);

    ps->insert_source[i][0]=bM;
    ps->insert_source[i][1]=bA;
    ps->insert_source[i][2]=bB;
    ps->insert_invert[i]=bI;

    att->s=multibar_slider_new(11,labels_dB,levels_dB,1);
    del->s=multibar_slider_new(6,labels_del,levels_del,1);
    att->r=readout_new("+00.0dB");
    del->r=readout_new("+00.0ms");
    att->val=&m->insert_att[i];
    del->val=&m->insert_delay[i];

    ps->sub[i]=multibar_new(6,labels_dBn,levels_dBn,0,
			    LO_ATTACK|LO_DECAY|HI_DECAY);
    
    ps->insert_att[i]=att;
    ps->insert_del[i]=del;

    multibar_callback(MULTIBAR(att->s),dB_slider_change,att);
    multibar_callback(MULTIBAR(del->s),ms_slider_change,del);

    gtk_box_pack_start(GTK_BOX(boxA),bM,1,1,0);
    gtk_box_pack_start(GTK_BOX(boxA),bA,1,1,0);
    gtk_box_pack_start(GTK_BOX(boxA),bB,1,1,0);

    g_signal_connect (G_OBJECT (bI), "clicked",
		      G_CALLBACK (toggle_callback), 
		      (gpointer)&m->insert_invert[i]);

    g_signal_connect (G_OBJECT (bM), "clicked",
		      G_CALLBACK (toggle_callback), 
		      (gpointer)&m->insert_source[i][0]);
    g_signal_connect (G_OBJECT (bA), "clicked",
		      G_CALLBACK (toggle_callback), 
		      (gpointer)&m->insert_source[i][1]);
    g_signal_connect (G_OBJECT (bB), "clicked",
		      G_CALLBACK (toggle_callback), 
		      (gpointer)&m->insert_source[i][2]);

    if(i==0){
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bM),1);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bA),1);
    }

    for(j=0;j<OUTPUT_CHANNELS;j++){
      char buffer[80];
      GtkWidget *b;
      
      sprintf(buffer," %d ",j+1);

      b=gtk_toggle_button_new_with_label(buffer);
      g_signal_connect (G_OBJECT (b), "clicked",
			G_CALLBACK (toggle_callback), 
			(gpointer)&m->insert_dest[i][j]);

      if(thisch%2 == j && i == 0)
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b),1);

      ps->insert_dest[i][j]=b;

      gtk_box_pack_start(GTK_BOX(boxB),b,1,1,0);
    }
    
    gtk_table_attach(GTK_TABLE(table),boxA,0,2,9+i*3,10+i*3,
		     GTK_FILL|GTK_EXPAND,0,0,0);
    gtk_table_attach(GTK_TABLE(table),boxB,5,6,9+i*3,10+i*3,
		     GTK_FILL|GTK_EXPAND,0,0,0);
    gtk_table_attach(GTK_TABLE(table),att->s,3,4,8+i*3,9+i*3,
		     GTK_FILL|GTK_EXPAND,0,2,0);
    gtk_table_attach(GTK_TABLE(table),del->s,3,4,9+i*3,10+i*3,
		     GTK_FILL|GTK_EXPAND,0,2,0);
    gtk_table_attach(GTK_TABLE(table),att->r,4,5,8+i*3,9+i*3,
		     GTK_FILL|GTK_EXPAND,0,0,0);
    gtk_table_attach(GTK_TABLE(table),del->r,4,5,9+i*3,10+i*3,
		     GTK_FILL|GTK_EXPAND,0,0,0);

    gtk_table_attach(GTK_TABLE(table),bI,0,2,8+i*3,9+i*3,
		     0,0,0,0);
    gtk_table_attach(GTK_TABLE(table),ps->sub[i],5,6,8+i*3,9+i*3,
		     GTK_FILL,0,0,0);

    gtk_table_attach(GTK_TABLE(table),latt,2,3,8+i*3,9+i*3,
		     GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(table),ldel,2,3,9+i*3,10+i*3,
		     GTK_FILL,0,0,0);

    gtk_table_attach(GTK_TABLE(table),h,0,6,10+i*3,11+i*3,
		     GTK_FILL,0,0,2);


  }

  gtk_box_pack_start(GTK_BOX(panel->subpanel_box),table,1,1,4);
  subpanel_show_all_but_toplevel(panel);

  return ps;
}

void mixpanel_create_channel(postfish_mainpanel *mp,
			    GtkWidget **windowbutton,
			    GtkWidget **activebutton){
  int i;
  mix_panels=malloc(input_ch*sizeof(*mix_panels));
  
  /* a panel for each channel */
  for(i=0;i<input_ch;i++){
    subpanel_generic *panel;
    char buffer[80];
    
    sprintf(buffer,"Mi_xdown block (channel %d)",i+1);
    
    panel=subpanel_create(mp,windowbutton[i],activebutton+i,
			  &mixpanel_active[i],
			  &mixpanel_visible[i],
			  buffer,0,i,1);
  
    mix_panels[i]=mixpanel_create_helper(mp,panel,mix_set+i,i);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(activebutton[i]),1);
  }
}

void attenpanel_create(postfish_mainpanel *mp,
		       GtkWidget **windowbutton,
		       GtkWidget **activebutton){
  int i;
  subpanel_generic *panel=subpanel_create(mp,windowbutton[0],activebutton,
					  mixpanel_active,
					  &atten_visible,
					  "Mi_x Input Delay / Attenuation",
					  0,0,input_ch);

  GtkWidget *table=gtk_table_new(input_ch*3+2,5,0);
  atten_panel.master=calloc(input_ch,sizeof(*atten_panel.master));  
  atten_panel.att=calloc(input_ch,sizeof(*atten_panel.att));
  atten_panel.del=calloc(input_ch,sizeof(*atten_panel.del));
  atten_panel.panel=panel;

  for(i=0;i<input_ch;i++){
    char buffer[80];
    GtkWidget *l1=gtk_label_new("attenuation ");
    GtkWidget *l2=gtk_label_new("delay ");
    GtkWidget *h=gtk_hseparator_new();

    sprintf(buffer," %d ",i+1);
    GtkWidget *lN=gtk_label_new(buffer);
    gtk_widget_set_name(lN,"framelabel");

    sprintf(buffer,"channel/reverb %d VU",i+1);
    GtkWidget *lV=gtk_label_new(buffer);
    
    slider_readout_pair *att=calloc(1,sizeof(*att));
    slider_readout_pair *del=calloc(1,sizeof(*del));
    
    atten_panel.master[i]=multibar_new(6,labels_dBn,levels_dBn,0,
				       LO_ATTACK|LO_DECAY|HI_DECAY);
    atten_panel.att[i]=att;
    atten_panel.del[i]=del;

    att->s=multibar_slider_new(11,labels_dB,levels_dB,1);
    att->r=readout_new("+00.0dB");
    att->val=&mix_set[i].master_att;
    
    del->s=multibar_slider_new(6,labels_del,levels_del,1);
    del->r=readout_new("+00.0ms");
    del->val=&mix_set[i].master_delay;
    
    multibar_callback(MULTIBAR(att->s),dB_slider_change,att);
    multibar_callback(MULTIBAR(del->s),ms_slider_change,del);

    multibar_thumb_set(MULTIBAR(att->s),0,0);
    multibar_thumb_set(MULTIBAR(del->s),0,0);

    gtk_misc_set_alignment(GTK_MISC(lN),1,.5);
    gtk_misc_set_alignment(GTK_MISC(l1),1,.5);
    gtk_misc_set_alignment(GTK_MISC(l2),1,.5);

    gtk_table_attach(GTK_TABLE(table),lN,0,1,0+i*3,2+i*3,
		     0,0,15,0);

    gtk_table_attach(GTK_TABLE(table),h,0,5,2+i*3,3+i*3,
    	     GTK_FILL|GTK_EXPAND,0,0,2);

    gtk_table_attach(GTK_TABLE(table),l1,1,2,0+i*3,1+i*3,
		     GTK_FILL|GTK_EXPAND,0,0,0);
    gtk_table_attach(GTK_TABLE(table),att->s,2,3,0+i*3,1+i*3,
		     GTK_FILL|GTK_EXPAND,0,0,0);
    gtk_table_attach(GTK_TABLE(table),att->r,3,4,0+i*3,1+i*3,
		     GTK_FILL|GTK_EXPAND,0,0,0);
    gtk_table_attach(GTK_TABLE(table),atten_panel.master[i],4,5,0+i*3,1+i*3,
		     GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(table),lV,4,5,1+i*3,2+i*3,
		     GTK_FILL,0,0,0);
    
    gtk_table_attach(GTK_TABLE(table),l2,1,2,1+i*3,2+i*3,
		     GTK_FILL|GTK_EXPAND,0,0,0);
    gtk_table_attach(GTK_TABLE(table),del->s,2,3,1+i*3,2+i*3,
		     GTK_FILL|GTK_EXPAND,0,0,0);
    gtk_table_attach(GTK_TABLE(table),del->r,3,4,1+i*3,2+i*3,
		     GTK_FILL|GTK_EXPAND,0,0,0);
    
  }

  /* average attenuation slider */
  {
    
    GtkWidget *l1=gtk_label_new("attenuation ");
    GtkWidget *lN=gtk_label_new("Avg");
    gtk_widget_set_name(lN,"framelabel");

    atten_panel.average=multibar_slider_new(11,labels_dB,levels_dB,1);

    multibar_callback(MULTIBAR(atten_panel.average),av_slider_change,&atten_panel);

    multibar_thumb_set(MULTIBAR(atten_panel.average),0,0);
    gtk_misc_set_alignment(GTK_MISC(lN),1,.5);
    gtk_misc_set_alignment(GTK_MISC(l1),1,.5);

    gtk_table_attach(GTK_TABLE(table),lN,0,1,0+i*3,1+i*3,
		     0,0,15,0);
    gtk_table_attach(GTK_TABLE(table),l1,1,2,0+i*3,1+i*3,
		     GTK_FILL|GTK_EXPAND,0,0,0);
    gtk_table_attach(GTK_TABLE(table),atten_panel.average,2,3,0+i*3,1+i*3,
		     GTK_FILL|GTK_EXPAND,0,0,0);


    gtk_table_set_row_spacing(GTK_TABLE(table),i*3-1,10);

    av_callback_enter=0; /* enable updates; not done earlier as
			    uncreated widgets would cause a segfualt */
  }

  gtk_box_pack_start(GTK_BOX(panel->subpanel_box),table,1,1,4);
  subpanel_show_all_but_toplevel(panel);
}


static float **peakfeed=0;
static float **rmsfeed=0;

void mixpanel_feedback(int displayit){
  int i,j;
  if(!peakfeed){
    peakfeed=malloc(sizeof(*peakfeed)*(MIX_BLOCKS+5));
    rmsfeed=malloc(sizeof(*rmsfeed)*(MIX_BLOCKS+5));

    for(i=0;i<(MIX_BLOCKS+5);i++){
      peakfeed[i]=malloc(sizeof(**peakfeed)*input_ch);
      rmsfeed[i]=malloc(sizeof(**rmsfeed)*input_ch);
    }
  }
  
  if(pull_mix_feedback(peakfeed,rmsfeed)==1){
    for(j=0;j<input_ch;j++){
      for(i=0;i<(MIX_BLOCKS+3);i++){
	float rms[input_ch+4];
	float peak[input_ch+4];
	
	memset(rms,0,sizeof(rms));
        memset(peak,0,sizeof(peak));
	
	switch(i){
	case 0:

	  /* master VU w/reverb display (3 channels) */
	  rms[1]=todB(rmsfeed[0][j])*.5;
	  peak[1]=todB(peakfeed[0][j])*.5;
	  rms[2]=todB(rmsfeed[1][j])*.5;
	  peak[2]=todB(peakfeed[1][j])*.5;
	  rms[3]=todB(rmsfeed[2][j])*.5;
	  peak[3]=todB(peakfeed[2][j])*.5;

	  multibar_set(MULTIBAR(atten_panel.master[j]),rms,peak,
		       4,(displayit && atten_visible));
	  break;

	case 2:
	case 1:

	  rms[j]=todB(rmsfeed[i+2][j])*.5;
	  peak[j]=todB(peakfeed[i+2][j])*.5;
	  multibar_set(MULTIBAR(mix_panels[j]->place[i-1]),rms,peak,
		       input_ch,(displayit && mixpanel_visible[j]));
	  break;
	default:
	  rms[j]=todB(rmsfeed[i+2][j])*.5;
	  peak[j]=todB(peakfeed[i+2][j])*.5;
	  multibar_set(MULTIBAR(mix_panels[j]->sub[i-3]),rms,peak,
		       input_ch,(displayit && mixpanel_visible[j]));
	  break;
	}
      }
    }
  }
}

void mixpanel_reset(void){
  int i,j;
  
  for(j=0;j<input_ch;j++){
    multibar_reset(MULTIBAR(atten_panel.master[j]));
    multibar_reset(MULTIBAR(mix_panels[j]->place[0]));
    multibar_reset(MULTIBAR(mix_panels[j]->place[1]));
    
    for(i=0;i<MIX_BLOCKS;i++)
      multibar_reset(MULTIBAR(mix_panels[j]->sub[i]));
  
  }
}

