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
#include "config.h"

typedef struct {
  GtkWidget *label;
  GtkWidget *slider;
  GtkWidget *readouto;
  GtkWidget *readoutu;
  struct multi_panel_state *mp;
  int number;
} cbar;

typedef struct{
  Multibar *s;
  Readout *r;
  sig_atomic_t *v;
} callback_arg_rv;

typedef struct{
  Multibar *s;
  Readout *r0;
  Readout *r1;
  sig_atomic_t *v0;
  sig_atomic_t *v1;
} callback_arg_rv2;

typedef struct{
  struct multi_panel_state *mp;
  int val;
} callback_arg_mi;

typedef struct multi_panel_state{
  subpanel_generic *panel;

  GtkWidget *over_rms;
  GtkWidget *over_peak;
  GtkWidget *over_softknee;

  GtkWidget *under_rms;
  GtkWidget *under_peak;
  GtkWidget *under_softknee;

  GtkWidget *base_rms;
  GtkWidget *base_peak;

  GtkWidget *octave[3];

  callback_arg_rv over_compand;
  callback_arg_rv under_compand;
  callback_arg_rv base_compand;

  callback_arg_rv over_lookahead;
  callback_arg_rv under_lookahead;

  callback_arg_rv2 over_timing;
  callback_arg_rv2 under_timing;
  callback_arg_rv2 base_timing;

  callback_arg_mi octave_full;
  callback_arg_mi octave_half;
  callback_arg_mi octave_two;

  int bank_active;
  int inactive_updatep;
  int updating_av_slider;
  multicompand_settings *ms;
  cbar bars[multicomp_freqs_max+1];

} multi_panel_state;

static multi_panel_state *master_panel;
static multi_panel_state **channel_panel;


static void compandpanel_state_to_config_helper(int bank,multicompand_settings *s,int A){
  int i;
  config_set_integer("multicompand_active",bank,A,0,0,0,s->panel_active);
  config_set_integer("multicompand_freqbank",bank,A,0,0,0,s->active_bank);
  for(i=0;i<multicomp_banks;i++){
    config_set_vector("multicompand_under_thresh",bank,A,i,0,multicomp_freqs_max,s->bc[i].static_u);
    config_set_vector("multicompand_over_thresh",bank,A,i,0,multicomp_freqs_max,s->bc[i].static_o);
  }

  config_set_integer("multicompand_over_set",bank,A,0,0,0,s->over_mode);
  config_set_integer("multicompand_over_set",bank,A,0,0,1,s->over_softknee);
  config_set_integer("multicompand_over_set",bank,A,0,0,2,s->over_ratio);
  config_set_integer("multicompand_over_set",bank,A,0,0,3,s->over_attack);
  config_set_integer("multicompand_over_set",bank,A,0,0,4,s->over_decay);
  config_set_integer("multicompand_over_set",bank,A,0,0,5,s->over_lookahead);

  config_set_integer("multicompand_under_set",bank,A,0,0,0,s->under_mode);
  config_set_integer("multicompand_under_set",bank,A,0,0,1,s->under_softknee);
  config_set_integer("multicompand_under_set",bank,A,0,0,2,s->under_ratio);
  config_set_integer("multicompand_under_set",bank,A,0,0,3,s->under_attack);
  config_set_integer("multicompand_under_set",bank,A,0,0,4,s->under_decay);
  config_set_integer("multicompand_under_set",bank,A,0,0,5,s->under_lookahead);

  config_set_integer("multicompand_base_set",bank,A,0,0,0,s->base_mode);
  config_set_integer("multicompand_base_set",bank,A,0,0,2,s->base_ratio);
  config_set_integer("multicompand_base_set",bank,A,0,0,3,s->base_attack);
  config_set_integer("multicompand_base_set",bank,A,0,0,4,s->base_decay);
}

void compandpanel_state_to_config(int bank){
  int i;
  compandpanel_state_to_config_helper(bank,&multi_master_set,0);
  for(i=0;i<input_ch;i++)
    compandpanel_state_to_config_helper(bank,multi_channel_set+i,i+1);
}

static void static_octave(GtkWidget *w,gpointer in);
static void propogate_bank_changes_full(multicompand_settings *ms,int from_bank);

static void compandpanel_state_from_config_helper(int bank,multicompand_settings *s,
						  multi_panel_state *p,int A){

  int i;
  config_get_sigat("multicompand_active",bank,A,0,0,0,&s->panel_active);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->panel->subpanel_activebutton[0]),s->panel_active);

  config_get_sigat("multicompand_freqbank",bank,A,0,0,0,&s->active_bank);
  for(i=0;i<multicomp_banks;i++){
    config_get_vector("multicompand_under_thresh",bank,A,i,0,multicomp_freqs_max,s->bc[i].static_u);
    config_get_vector("multicompand_over_thresh",bank,A,i,0,multicomp_freqs_max,s->bc[i].static_o);
  }

  config_get_sigat("multicompand_over_set",bank,A,0,0,0,&s->over_mode);
  if(s->over_mode)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->over_peak),1);
  else
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->over_rms),1);

  config_get_sigat("multicompand_over_set",bank,A,0,0,1,&s->over_softknee);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->over_softknee),s->over_softknee);
  config_get_sigat("multicompand_over_set",bank,A,0,0,2,&s->over_ratio);
  multibar_thumb_set(p->over_compand.s,1000./s->over_ratio,0);
  config_get_sigat("multicompand_over_set",bank,A,0,0,3,&s->over_attack);
  multibar_thumb_set(p->over_timing.s,s->over_attack*.1,0);
  config_get_sigat("multicompand_over_set",bank,A,0,0,4,&s->over_decay);
  multibar_thumb_set(p->over_timing.s,s->over_decay*.1,1);
  config_get_sigat("multicompand_over_set",bank,A,0,0,5,&s->over_lookahead);
  multibar_thumb_set(p->over_lookahead.s,s->over_lookahead*.1,0);

  config_get_sigat("multicompand_under_set",bank,A,0,0,0,&s->under_mode);
  if(s->under_mode)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->under_peak),1);
  else
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->under_rms),1);

  config_get_sigat("multicompand_under_set",bank,A,0,0,1,&s->under_softknee);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->under_softknee),s->under_softknee);
  config_get_sigat("multicompand_under_set",bank,A,0,0,2,&s->under_ratio);
  multibar_thumb_set(p->under_compand.s,1000./s->under_ratio,0);
  config_get_sigat("multicompand_under_set",bank,A,0,0,3,&s->under_attack);
  multibar_thumb_set(p->under_timing.s,s->under_attack*.1,0);
  config_get_sigat("multicompand_under_set",bank,A,0,0,4,&s->under_decay);
  multibar_thumb_set(p->under_timing.s,s->under_decay*.1,1);
  config_get_sigat("multicompand_under_set",bank,A,0,0,5,&s->under_lookahead);
  multibar_thumb_set(p->under_lookahead.s,s->under_lookahead*.1,0);

  config_get_sigat("multicompand_base_set",bank,A,0,0,0,&s->base_mode);
  if(s->base_mode)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->base_peak),1);
  else
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->base_rms),1);
  config_get_sigat("multicompand_base_set",bank,A,0,0,2,&s->base_ratio);
  multibar_thumb_set(p->base_compand.s,1000./s->base_ratio,0);
  config_get_sigat("multicompand_base_set",bank,A,0,0,3,&s->base_attack);
  multibar_thumb_set(p->base_timing.s,s->base_attack*.1,0);
  config_get_sigat("multicompand_base_set",bank,A,0,0,4,&s->base_decay);
  multibar_thumb_set(p->base_timing.s,s->base_decay*.1,1);

  propogate_bank_changes_full(s,s->active_bank);
  /* setting the active bank also redisplays all the sliders */
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->octave[s->active_bank]),1);
  /* safe to call blindly; if the above already triggered the work, this is a no op */
  switch(s->active_bank){  
  case 0:
    static_octave(0,&p->octave_full);
    break;
  case 1:
    static_octave(0,&p->octave_half);
    break;
  case 2:
    static_octave(0,&p->octave_two);
    break;
  }
}

void compandpanel_state_from_config(int bank){
  int i;
  compandpanel_state_from_config_helper(bank,&multi_master_set,master_panel,0);
  for(i=0;i<input_ch;i++)
    compandpanel_state_from_config_helper(bank,multi_channel_set+i,channel_panel[i],i+1);
}


static void compand_change(GtkWidget *w,gpointer in){
  callback_arg_rv *ca=(callback_arg_rv *)in;
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

  readout_set(ca->r,buffer);
  
  *ca->v=rint(val*1000.);
}

static void timing_change(GtkWidget *w,gpointer in){
  callback_arg_rv2 *ca=(callback_arg_rv2 *)in;

  float attack=multibar_get_value(MULTIBAR(w),0);
  float decay=multibar_get_value(MULTIBAR(w),1);
  char buffer[80];

  if(attack<10){
    sprintf(buffer,"%4.2fms",attack);
  }else if(attack<100){
    sprintf(buffer,"%4.1fms",attack);
  }else if (attack<1000){
    sprintf(buffer,"%4.0fms",attack);
  }else if (attack<10000){
    sprintf(buffer,"%4.2fs",attack/1000.);
  }else{
    sprintf(buffer,"%4.1fs",attack/1000.);
  }
  readout_set(ca->r0,buffer);

  if(decay<10){
    sprintf(buffer,"%4.2fms",decay);
  }else if(decay<100){
    sprintf(buffer,"%4.1fms",decay);
  }else if (decay<1000){
    sprintf(buffer,"%4.0fms",decay);
  }else if (decay<10000){
    sprintf(buffer,"%4.2fs",decay/1000.);
  }else{
    sprintf(buffer,"%4.1fs",decay/1000.);
  }
  readout_set(ca->r1,buffer);

  *ca->v0=rint(attack*10.);
  *ca->v1=rint(decay*10.);
}

static void lookahead_change(GtkWidget *w,gpointer in){
  callback_arg_rv *ca=(callback_arg_rv *)in;
  char buffer[80];
  Readout *r=ca->r;
  float val=multibar_get_value(MULTIBAR(w),0);

  sprintf(buffer,"%3.0f%%",val);
  readout_set(r,buffer);
  
  *ca->v=rint(val*10.);
}

static int determine_average(multi_panel_state *mp,int x){
  multicompand_settings *ms=mp->ms;
  banked_multicompand_settings *bc=ms->bc;
  int bank_active=mp->bank_active;
  float acc=0;
  int i;

  for(i=0;i<multicomp_freqs[bank_active];i++){
    if(x==1)
      acc+=rint(bc[bank_active].static_o[i]);
    else
      acc+=rint(bc[bank_active].static_u[i]);
  }
  return rint(acc/multicomp_freqs[bank_active]);
}

static void average_change(GtkWidget *w,gpointer in){
  multi_panel_state *mp=(multi_panel_state *)in;
  if(!mp->updating_av_slider){
    cbar *b=mp->bars+multicomp_freqs_max;
    cbar *bars=mp->bars;
    int bank_active=mp->bank_active;
    
    int i;
    int o,u;
    int ud,od;

    u=rint(multibar_get_value(MULTIBAR(b->slider),0));
    o=rint(multibar_get_value(MULTIBAR(b->slider),1));
        
    ud = u-determine_average(mp,0);
    od = o-determine_average(mp,1);
    
    mp->updating_av_slider=1;

    if(od<0 && ud<0)ud=0;
    if(od>0 && ud>0)od=0;

    if(multibar_thumb_focus(MULTIBAR(w))==1){
      /* update o sliders */
      for(i=0;i<multicomp_freqs[bank_active];i++){
	float val=multibar_get_value(MULTIBAR(bars[i].slider),1);
	multibar_thumb_set(MULTIBAR(bars[i].slider),val+od,1);
      }
      /* update u average (might have pushed it via its sliders) */
      u = determine_average(mp,0);
      multibar_thumb_set(MULTIBAR(bars[multicomp_freqs_max].slider),u,0);
    }else if(multibar_thumb_focus(MULTIBAR(w))==0){
      /* update u sliders */
      for(i=0;i<multicomp_freqs[bank_active];i++){
	float val=multibar_get_value(MULTIBAR(bars[i].slider),0);
	multibar_thumb_set(MULTIBAR(bars[i].slider),val+ud,0);
      }
      /* update o average (might have pushed it via its sliders) */
      o = determine_average(mp,1);
      multibar_thumb_set(MULTIBAR(bars[multicomp_freqs_max].slider),o,1);
    }
    mp->updating_av_slider=0;
  }
}

static void slider_change(GtkWidget *w,gpointer in){
  char buffer[80];
  cbar *b=(cbar *)in;
  multi_panel_state *mp=b->mp;
  multicompand_settings *ms=mp->ms;
  banked_multicompand_settings *bc=ms->bc;
  int bank_active=mp->bank_active;

  int o,u;

  u=multibar_get_value(MULTIBAR(b->slider),0);
  sprintf(buffer,"%+4ddB",u);
  readout_set(READOUT(b->readoutu),buffer);
  bc[bank_active].static_u[b->number]=u;
  
  o=multibar_get_value(MULTIBAR(b->slider),1);
  sprintf(buffer,"%+4ddB",o);
  readout_set(READOUT(b->readouto),buffer);
  bc[bank_active].static_o[b->number]=o;

  /* update average slider */
  if(!mp->updating_av_slider && mp->bars[multicomp_freqs_max].slider){
    float oav=0,uav=0;
    mp->updating_av_slider=1;
    
    /* compute the current average */
    uav = determine_average(mp,0);
    oav = determine_average(mp,1);
    
    multibar_thumb_set(MULTIBAR(mp->bars[multicomp_freqs_max].slider),uav,0);
    multibar_thumb_set(MULTIBAR(mp->bars[multicomp_freqs_max].slider),oav,1);
    mp->updating_av_slider=0;
  }
}

static void propogate_bank_changes(multicompand_settings *ms,int bank_active, int to_bank){
  banked_multicompand_settings *bc=ms->bc;
  int i;

  /* propogate changes from current bank to new bank */
  switch(bank_active*10+to_bank){
  case 1:  /* full octave to half octave */
  case 20: /* two octave to full octave */
    
    for(i=0;i<multicomp_freqs[bank_active];i++){
      float u0d=bc[bank_active].static_u[i]-bc[to_bank].static_u[i*2];
      float u2d=(i+1<multicomp_freqs[bank_active] ? 
		 bc[bank_active].static_u[i+1]-bc[to_bank].static_u[i*2+2] : u0d);
      
      float o0d=bc[bank_active].static_o[i]-bc[to_bank].static_o[i*2];
      float o2d=(i+1<multicomp_freqs[bank_active] ? 
		 bc[bank_active].static_o[i+1]-bc[to_bank].static_o[i*2+2] : o0d);
      
      bc[to_bank].static_u[i*2+1] += (u0d+u2d)/2;
      bc[to_bank].static_o[i*2+1] += (o0d+o2d)/2;
    }
    
    for(i=0;i<multicomp_freqs[bank_active];i++){
      bc[to_bank].static_u[i*2]=bc[bank_active].static_u[i];
      bc[to_bank].static_o[i*2]=bc[bank_active].static_o[i];
    }
    break;
    
  case 2: /* full octave to two octave */
  case 10: /* half octave to full octave */
    
    for(i=0;i<multicomp_freqs[to_bank];i++){
      bc[to_bank].static_u[i]=bc[bank_active].static_u[i*2];
      bc[to_bank].static_o[i]=bc[bank_active].static_o[i*2];
    }
    break;
    
  case 12: /* half octave to two octave */
    
    for(i=0;i<multicomp_freqs[to_bank];i++){
      bc[to_bank].static_u[i]=bc[bank_active].static_u[i*4];
      bc[to_bank].static_o[i]=bc[bank_active].static_o[i*4];
    }
    break;
    
  case 21: /* two octave to half octave */
    
    for(i=0;i<multicomp_freqs[bank_active];i++){
      float u0d=bc[bank_active].static_u[i]-bc[to_bank].static_u[i*4];
      float u2d=(i+1<multicomp_freqs[bank_active] ? 
		 bc[bank_active].static_u[i+1]-bc[to_bank].static_u[i*4+4] : u0d);
      
      float o0d=bc[bank_active].static_o[i]-bc[to_bank].static_o[i*4];
      float o2d=(i+1<multicomp_freqs[bank_active] ? 
		 bc[bank_active].static_o[i+1]-bc[to_bank].static_o[i*4+4] : o0d);
      
      bc[to_bank].static_u[i*4+1] += (u0d*3+u2d)/4;
      bc[to_bank].static_o[i*4+1] += (o0d*3+o2d)/4;
      
      bc[to_bank].static_u[i*4+2] += (u0d+u2d)/2;
      bc[to_bank].static_o[i*4+2] += (o0d+o2d)/2;
      
      bc[to_bank].static_u[i*4+3] += (u0d+u2d*3)/4;
      bc[to_bank].static_o[i*4+3] += (o0d+o2d*3)/4;
    }
    
    for(i=0;i<multicomp_freqs[bank_active];i++){
      bc[to_bank].static_u[i*4]=bc[bank_active].static_u[i];
      bc[to_bank].static_o[i*4]=bc[bank_active].static_o[i];
    }
    break;
  }
}

static void propogate_bank_changes_full(multicompand_settings *ms,int from_bank){
  switch(from_bank){
  case 0:
    propogate_bank_changes(ms,0,1);
    propogate_bank_changes(ms,0,2);
    break;
  case 1:
    propogate_bank_changes(ms,1,0);
    propogate_bank_changes(ms,1,2);
    break;
  case 2:
    propogate_bank_changes(ms,2,0);
    propogate_bank_changes(ms,2,1);
    break;
  }
}

static void static_octave(GtkWidget *w,gpointer in){
  callback_arg_mi *ca=(callback_arg_mi *)in;
  multi_panel_state *mp=ca->mp;
  multicompand_settings *ms=mp->ms;
  banked_multicompand_settings *bc=ms->bc;
  int octave=ca->val,i;

  if(!w || gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))){
    if(mp->bank_active!=octave || w==NULL){
      int bank_active;
      propogate_bank_changes(ms,mp->bank_active,octave);

      bank_active=mp->bank_active=octave;
      
      /* map, unmap, relabel */
      for(i=0;i<multicomp_freqs_max;i++){
	if(i<multicomp_freqs[bank_active]){
	  gtk_label_set_text(GTK_LABEL(mp->bars[i].label),
			     multicomp_freq_labels[bank_active][i]);
	  gtk_widget_show(mp->bars[i].label);
	  gtk_widget_show(mp->bars[i].slider);
	  gtk_widget_show(mp->bars[i].readouto);
	  gtk_widget_show(mp->bars[i].readoutu);
	  
	  mp->inactive_updatep=0;
	  
	  {
	    float o=bc[bank_active].static_o[i];
	    float u=bc[bank_active].static_u[i];
	    
	    multibar_thumb_set(MULTIBAR(mp->bars[i].slider),u,0);
	    multibar_thumb_set(MULTIBAR(mp->bars[i].slider),o,1);
	  }

	  mp->inactive_updatep=1;
	  
	}else{
	  gtk_widget_hide(mp->bars[i].label);
	  gtk_widget_hide(mp->bars[i].slider);
	  gtk_widget_hide(mp->bars[i].readouto);
	  gtk_widget_hide(mp->bars[i].readoutu);
	}
      }

      ms->active_bank=bank_active;
      
    }
  }
}

static void mode_rms(GtkButton *b,gpointer in){
  sig_atomic_t *var=(sig_atomic_t *)in;
  *var=0;
}

static void mode_peak(GtkButton *b,gpointer in){
  sig_atomic_t *var=(sig_atomic_t *)in;
  *var=1;
}

static void mode_knee(GtkToggleButton *b,gpointer in){
  int mode=gtk_toggle_button_get_active(b);
  sig_atomic_t *var=(sig_atomic_t *)in;
  *var=mode;
}

static multi_panel_state *compandpanel_create(postfish_mainpanel *mp,
					      subpanel_generic *panel,
					      multicompand_settings *ms){
  int i;
  char *labels[15]={"","130","120","110","100","90","80","70",
		    "60","50","40","30","20","10","0"};
  float levels[15]={-140,-130,-120,-110,-100,-90,-80,-70,-60,-50,-40,
		     -30,-20,-10,0};

  float compand_levels[9]={.1,.25,.5,.6667,1,1.5,2,4,10};
  char  *compand_labels[89]={"","4:1","2:1","1:1.5","1:1","1:1.5","1:2","1:4","1:10"};

  float timing_levels[6]={.5,1,10,100,1000,10000};
  char  *timing_labels[6]={"","1ms","10ms","100ms","1s","10s"};

  float per_levels[9]={0,12.5,25,37.5,50,62.5,75,87.5,100};
  char  *per_labels[9]={"0%","","25%","","50%","","75%","","100%"};

  multi_panel_state *ps=calloc(1,sizeof(*ps));
  ps->inactive_updatep=1;
  ps->bank_active=2;
  ps->ms=ms;
  ps->panel=panel;

  GtkWidget *hbox=gtk_hbox_new(0,0);
  GtkWidget *sliderbox=gtk_vbox_new(0,0);
  GtkWidget *sliderframe=gtk_frame_new(NULL);
  GtkWidget *staticbox=gtk_vbox_new(0,0);
  GtkWidget *slidertable=gtk_table_new(multicomp_freqs_max+2,4,0);

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
  
    GtkWidget *octave_box=gtk_hbox_new(0,0);
    GtkWidget *octave_a=gtk_radio_button_new_with_label(NULL,"two-octave");
    GtkWidget *octave_b=
      gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(octave_a),
						  "full-octave");
    GtkWidget *octave_c=
      gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(octave_b),
						  "half-octave");
    GtkWidget *label2=gtk_label_new("under");
    GtkWidget *label3=gtk_label_new("over");

    gtk_misc_set_alignment(GTK_MISC(label2),.5,1.);
    gtk_misc_set_alignment(GTK_MISC(label3),.5,1.);
    gtk_widget_set_name(label2,"scalemarker");
    gtk_widget_set_name(label3,"scalemarker");


    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(octave_b),0);

    ps->octave_full.mp=ps;
    ps->octave_full.val=0;
    ps->octave_half.mp=ps;
    ps->octave_half.val=1;
    ps->octave_two.mp=ps;
    ps->octave_two.val=2;

    g_signal_connect (G_OBJECT (octave_a), "clicked",
		      G_CALLBACK (static_octave), &ps->octave_two);
    g_signal_connect (G_OBJECT (octave_b), "clicked",
		      G_CALLBACK (static_octave), &ps->octave_full);
    g_signal_connect (G_OBJECT (octave_c), "clicked",
		      G_CALLBACK (static_octave), &ps->octave_half);
   
    gtk_table_attach(GTK_TABLE(slidertable),label2,1,2,0,1,GTK_FILL,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(slidertable),label3,3,4,0,1,GTK_FILL,GTK_FILL|GTK_EXPAND,2,0);
 
    gtk_box_pack_start(GTK_BOX(octave_box),octave_a,0,1,3);
    gtk_box_pack_start(GTK_BOX(octave_box),octave_b,0,1,3);
    gtk_box_pack_start(GTK_BOX(octave_box),octave_c,0,1,3);
    gtk_table_attach(GTK_TABLE(slidertable),octave_box,2,3,0,1,
		     GTK_EXPAND,0,0,0);

    gtk_box_pack_start(GTK_BOX(sliderbox),sliderframe,0,0,0);
    gtk_container_add(GTK_CONTAINER(sliderframe),slidertable);

    gtk_frame_set_shadow_type(GTK_FRAME(sliderframe),GTK_SHADOW_NONE);
    
    gtk_container_set_border_width(GTK_CONTAINER(sliderframe),4);

    ps->octave[0]=octave_b;
    ps->octave[1]=octave_c;
    ps->octave[2]=octave_a;

  }

  gtk_box_pack_start(GTK_BOX(panel->subpanel_box),hbox,0,0,0);
  gtk_box_pack_start(GTK_BOX(hbox),sliderbox,0,0,0);
  gtk_box_pack_start(GTK_BOX(hbox),staticbox,0,0,0);

  {
    GtkWidget *hs1=gtk_hseparator_new();
    GtkWidget *hs2=gtk_hseparator_new();
    GtkWidget *hs3=gtk_hseparator_new();

    gtk_box_pack_start(GTK_BOX(staticbox),overtable,0,0,10);
    gtk_box_pack_start(GTK_BOX(staticbox),hs1,0,0,0);
    gtk_box_pack_start(GTK_BOX(staticbox),undertable,0,0,10);
    gtk_box_pack_start(GTK_BOX(staticbox),hs2,0,0,0);
    gtk_box_pack_start(GTK_BOX(staticbox),basetable,0,0,10);
    gtk_box_pack_start(GTK_BOX(staticbox),hs3,0,0,0);

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
		      G_CALLBACK (mode_knee), &ps->ms->under_softknee);
    g_signal_connect (G_OBJECT (rms_button), "clicked",
		      G_CALLBACK (mode_rms), &ps->ms->under_mode);
    g_signal_connect (G_OBJECT (peak_button), "clicked",
		      G_CALLBACK (mode_peak), &ps->ms->under_mode);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rms_button),1);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(knee_button),1);
    gtk_table_attach(GTK_TABLE(undertable),envelopebox,0,4,0,1,GTK_FILL,0,0,0);
    ps->under_rms=rms_button;
    ps->under_peak=peak_button;
    ps->under_softknee=knee_button;
  }

  /* under compand: ratio */
  {

    GtkWidget *label=gtk_label_new("compand ratio:");
    GtkWidget *readout=readout_new("1.55:1");
    GtkWidget *slider=multibar_slider_new(9,compand_labels,compand_levels,1);
   
    ps->under_compand.s=MULTIBAR(slider);
    ps->under_compand.r=READOUT(readout);
    ps->under_compand.v=&ps->ms->under_ratio;

    multibar_callback(MULTIBAR(slider),compand_change,&ps->under_compand);
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
    GtkWidget *readout0=readout_new(" 100ms");
    GtkWidget *readout1=readout_new(" 100ms");
    GtkWidget *slider=multibar_slider_new(6,timing_labels,timing_levels,2);

    ps->under_timing.s=MULTIBAR(slider);
    ps->under_timing.r0=READOUT(readout0);
    ps->under_timing.r1=READOUT(readout1);
    ps->under_timing.v0=&ps->ms->under_attack;
    ps->under_timing.v1=&ps->ms->under_decay;
   
    multibar_callback(MULTIBAR(slider),timing_change,&ps->under_timing);
    multibar_thumb_set(MULTIBAR(slider),1,0);
    multibar_thumb_set(MULTIBAR(slider),100,1);

    gtk_misc_set_alignment(GTK_MISC(label),1,.5);

    gtk_table_set_row_spacing(GTK_TABLE(undertable),2,4);
    gtk_table_attach(GTK_TABLE(undertable),label,0,1,4,5,GTK_FILL,0,2,0);
    gtk_table_attach(GTK_TABLE(undertable),slider,1,2,4,5,GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(undertable),readout0,2,3,4,5,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(undertable),readout1,3,4,4,5,GTK_FILL,0,0,0);

  }

  /* under compand: lookahead */
  {

    GtkWidget *label=gtk_label_new("lookahead:");
    GtkWidget *readout=readout_new("100%");
    GtkWidget *slider=multibar_slider_new(9,per_labels,per_levels,1);

    ps->under_lookahead.s=MULTIBAR(slider);
    ps->under_lookahead.r=READOUT(readout);
    ps->under_lookahead.v=&ps->ms->under_lookahead;

    multibar_callback(MULTIBAR(slider),lookahead_change,&ps->under_lookahead);
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
		      G_CALLBACK (mode_knee), &ps->ms->over_softknee);
    g_signal_connect (G_OBJECT (rms_button), "clicked",
		      G_CALLBACK (mode_rms), &ps->ms->over_mode);
    g_signal_connect (G_OBJECT (peak_button), "clicked",
		      G_CALLBACK (mode_peak), &ps->ms->over_mode);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rms_button),1);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(knee_button),1);
    gtk_table_attach(GTK_TABLE(overtable),envelopebox,0,4,0,1,GTK_FILL,0,0,0);
    ps->over_rms=rms_button;
    ps->over_peak=peak_button;
    ps->over_softknee=knee_button;
  }

  /* over compand: ratio */
  {

    GtkWidget *label=gtk_label_new("compand ratio:");
    GtkWidget *readout=readout_new("1.55:1");
    GtkWidget *slider=multibar_slider_new(9,compand_labels,compand_levels,1);
   
    ps->over_compand.s=MULTIBAR(slider);
    ps->over_compand.r=READOUT(readout);
    ps->over_compand.v=&ps->ms->over_ratio;

    multibar_callback(MULTIBAR(slider),compand_change,&ps->over_compand);
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
    GtkWidget *readout0=readout_new(" 100ms");
    GtkWidget *readout1=readout_new(" 100ms");
    GtkWidget *slider=multibar_slider_new(6,timing_labels,timing_levels,2);
   
    ps->over_timing.s=MULTIBAR(slider);
    ps->over_timing.r0=READOUT(readout0);
    ps->over_timing.r1=READOUT(readout1);
    ps->over_timing.v0=&ps->ms->over_attack;
    ps->over_timing.v1=&ps->ms->over_decay;
   
    multibar_callback(MULTIBAR(slider),timing_change,&ps->over_timing);
    multibar_thumb_set(MULTIBAR(slider),1,0);
    multibar_thumb_set(MULTIBAR(slider),100,1);

    gtk_misc_set_alignment(GTK_MISC(label),1,.5);

    gtk_table_set_row_spacing(GTK_TABLE(overtable),2,4);
    gtk_table_attach(GTK_TABLE(overtable),label,0,1,5,6,GTK_FILL,0,2,0);
    gtk_table_attach(GTK_TABLE(overtable),slider,1,2,5,6,GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(overtable),readout0,2,3,5,6,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(overtable),readout1,3,4,5,6,GTK_FILL,0,0,0);

  }

  /* over compand: lookahead */
  {

    GtkWidget *label=gtk_label_new("lookahead:");
    GtkWidget *readout=readout_new("100%");
    GtkWidget *slider=multibar_slider_new(9,per_labels,per_levels,1);
   
    ps->over_lookahead.s=MULTIBAR(slider);
    ps->over_lookahead.r=READOUT(readout);
    ps->over_lookahead.v=&ps->ms->over_lookahead;

    multibar_callback(MULTIBAR(slider),lookahead_change,&ps->over_lookahead);
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
		      G_CALLBACK (mode_rms), &ps->ms->base_mode);
    g_signal_connect (G_OBJECT (peak_button), "clicked",
		      G_CALLBACK (mode_peak), &ps->ms->base_mode);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rms_button),1);
    gtk_table_attach(GTK_TABLE(basetable),envelopebox,0,4,0,1,GTK_FILL,0,0,0);
    ps->base_rms=rms_button;
    ps->base_peak=peak_button;
  }

  /* base compand: ratio */
  {

    GtkWidget *label=gtk_label_new("compand ratio:");
    GtkWidget *readout=readout_new("1.55:1");
    GtkWidget *slider=multibar_slider_new(9,compand_labels,compand_levels,1);
   
    ps->base_compand.s=MULTIBAR(slider);
    ps->base_compand.r=READOUT(readout);
    ps->base_compand.v=&ps->ms->base_ratio;

    multibar_callback(MULTIBAR(slider),compand_change,&ps->base_compand);
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
    GtkWidget *readout0=readout_new(" 100ms");
    GtkWidget *readout1=readout_new(" 100ms");
    GtkWidget *slider=multibar_slider_new(6,timing_labels,timing_levels,2);

    ps->base_timing.s=MULTIBAR(slider);
    ps->base_timing.r0=READOUT(readout0);
    ps->base_timing.r1=READOUT(readout1);
    ps->base_timing.v0=&ps->ms->base_attack;
    ps->base_timing.v1=&ps->ms->base_decay;
   
    multibar_callback(MULTIBAR(slider),timing_change,&ps->base_timing);
    multibar_thumb_set(MULTIBAR(slider),1,0);
    multibar_thumb_set(MULTIBAR(slider),100,1);

    gtk_misc_set_alignment(GTK_MISC(label),1,.5);

    gtk_table_set_row_spacing(GTK_TABLE(basetable),2,4);
    gtk_table_attach(GTK_TABLE(basetable),label,0,1,4,5,GTK_FILL,0,2,0);
    gtk_table_attach(GTK_TABLE(basetable),slider,1,2,4,5,GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,2,0);
    gtk_table_attach(GTK_TABLE(basetable),readout0,2,3,4,5,GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(basetable),readout1,3,4,4,5,GTK_FILL,0,0,0);

  }

  /* threshold controls */

  for(i=0;i<multicomp_freqs_max;i++){
    GtkWidget *label=gtk_label_new(NULL);
    gtk_widget_set_name(label,"scalemarker");
    
    ps->bars[i].readoutu=readout_new("  +0");
    ps->bars[i].readouto=readout_new("  +0");
    ps->bars[i].slider=multibar_new(15,labels,levels,2,HI_DECAY|LO_DECAY|LO_ATTACK);
    ps->bars[i].number=i;
    ps->bars[i].mp=ps;
    ps->bars[i].label=label;

    multibar_callback(MULTIBAR(ps->bars[i].slider),slider_change,ps->bars+i);
    multibar_thumb_set(MULTIBAR(ps->bars[i].slider),-140.,0);
    multibar_thumb_set(MULTIBAR(ps->bars[i].slider),0.,1);
    multibar_thumb_bounds(MULTIBAR(ps->bars[i].slider),-140,0);
    multibar_thumb_increment(MULTIBAR(ps->bars[i].slider),1.,10.);
    
    gtk_misc_set_alignment(GTK_MISC(label),1,.5);
      
    gtk_table_attach(GTK_TABLE(slidertable),label,0,1,i+1,i+2,
		     GTK_FILL,0,10,0);
    gtk_table_attach(GTK_TABLE(slidertable),ps->bars[i].readoutu,1,2,i+1,i+2,
		     0,0,0,0);
    gtk_table_attach(GTK_TABLE(slidertable),ps->bars[i].slider,2,3,i+1,i+2,
		     GTK_FILL|GTK_EXPAND,GTK_EXPAND,0,0);
    gtk_table_attach(GTK_TABLE(slidertable),ps->bars[i].readouto,3,4,i+1,i+2,
		     0,0,0,0);
  }

  /* "average" slider */
  {
    GtkWidget *label=gtk_label_new("average");
    
    ps->bars[multicomp_freqs_max].slider=multibar_slider_new(15,labels,levels,2);

    multibar_callback(MULTIBAR(ps->bars[multicomp_freqs_max].slider),average_change,ps);

    multibar_thumb_set(MULTIBAR(ps->bars[multicomp_freqs_max].slider),-140.,0);
    multibar_thumb_set(MULTIBAR(ps->bars[multicomp_freqs_max].slider),0.,1);
    multibar_thumb_bounds(MULTIBAR(ps->bars[multicomp_freqs_max].slider),-140,0);
    
    gtk_table_attach(GTK_TABLE(slidertable),label,0,2,multicomp_freqs_max+1,multicomp_freqs_max+2,
		     GTK_FILL,0,0,0);
    gtk_table_attach(GTK_TABLE(slidertable),ps->bars[i].slider,2,3,multicomp_freqs_max+1,multicomp_freqs_max+2,
		     GTK_FILL|GTK_EXPAND,GTK_EXPAND,0,0);
    gtk_table_set_row_spacing(GTK_TABLE(slidertable),multicomp_freqs_max,10);
  }

  
  subpanel_show_all_but_toplevel(panel);

  /* Now unmap the sliders we don't want */
  static_octave(NULL,&ps->octave_full);

  return ps;
}

void compandpanel_create_master(postfish_mainpanel *mp,
				GtkWidget *windowbutton,
				GtkWidget *activebutton){
  char *shortcut[]={" m "};
  subpanel_generic *panel=subpanel_create(mp,windowbutton,&activebutton,
					  &multi_master_set.panel_active,
					  &multi_master_set.panel_visible,
					  "_Multiband Compand (master)",shortcut,
					  0,1);

  master_panel=compandpanel_create(mp,panel,&multi_master_set);
}

void compandpanel_create_channel(postfish_mainpanel *mp,
				 GtkWidget **windowbutton,
				 GtkWidget **activebutton){
  int i;
  
  channel_panel=calloc(input_ch,sizeof(*channel_panel));

  /* a panel for each channel */
  for(i=0;i<input_ch;i++){
    subpanel_generic *panel;
    char buffer[80];

    sprintf(buffer,"_Multiband Compand (channel %d)",i+1);
    panel=subpanel_create(mp,windowbutton[i],activebutton+i,
			  &multi_channel_set[i].panel_active,
			  &multi_channel_set[i].panel_visible,
			  buffer,NULL,
			  i,1);
    
    channel_panel[i]=compandpanel_create(mp,panel,multi_channel_set+i);
  }
}


static float **peakfeed=0;
static float **rmsfeed=0;

void compandpanel_feedback(int displayit){
  int i,j,bands;
  if(!peakfeed){
    peakfeed=malloc(sizeof(*peakfeed)*multicomp_freqs_max);
    rmsfeed=malloc(sizeof(*rmsfeed)*multicomp_freqs_max);

    for(i=0;i<multicomp_freqs_max;i++){
      peakfeed[i]=malloc(sizeof(**peakfeed)*max(input_ch,OUTPUT_CHANNELS));
      rmsfeed[i]=malloc(sizeof(**rmsfeed)*max(input_ch,OUTPUT_CHANNELS));
    }
  }

  if(pull_multicompand_feedback_master(peakfeed,rmsfeed,&bands)==1)
    for(i=0;i<bands;i++)
      multibar_set(MULTIBAR(master_panel->bars[i].slider),rmsfeed[i],peakfeed[i],
		   OUTPUT_CHANNELS,(displayit && multi_master_set.panel_visible));

  /* channel panels are a bit different; we want each in its native color */
  if(pull_multicompand_feedback_channel(peakfeed,rmsfeed,&bands)==1){
    for(j=0;j<input_ch;j++){
      for(i=0;i<bands;i++){
	float rms[input_ch];
	float peak[input_ch];
	
	memset(rms,0,sizeof(rms));
	memset(peak,0,sizeof(peak));
	rms[j]=rmsfeed[i][j];
	peak[j]=peakfeed[i][j];
	
	multibar_set(MULTIBAR(channel_panel[j]->bars[i].slider),rms,peak,
		     input_ch,(displayit && multi_channel_set[j].panel_visible));
      }
    }
  }
}

void compandpanel_reset(void){
  int i,j;
  for(i=0;i<multicomp_freqs_max;i++)
    multibar_reset(MULTIBAR(master_panel->bars[i].slider));

  for(i=0;i<multicomp_freqs_max;i++)
    for(j=0;j<input_ch;j++)
      multibar_reset(MULTIBAR(channel_panel[j]->bars[i].slider));
}


