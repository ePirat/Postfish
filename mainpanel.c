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
#include "fisharray.h"
#include "buttonicons.h"
#include "multibar.h"
#include "readout.h"
#include "input.h"
#include "output.h"
#include "mainpanel.h"
#include "windowbutton.h"
#include "config.h"

static postfish_mainpanel p;
extern char *configfile;
extern sig_atomic_t main_looping;

static void action_setb_to(postfish_mainpanel *p,const char *time);
static void action_seta_to(postfish_mainpanel *p,const char *time);


static void mainpanel_state_to_config(int bank){
  int i;
  float f;

  f=multibar_get_value(MULTIBAR(p.masterdB_s),0);
  i=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p.masterdB_a));

  config_set_integer("mainpanel_master_att",bank,0,0,0,0,rint(f*10));
  config_set_integer("mainpanel_master_att_active",bank,0,0,0,0,i);

  config_set_string("mainpanel_cue_A",0,0,0,0,gtk_entry_get_text(GTK_ENTRY(p.entry_a)));
  config_set_string("mainpanel_cue_B",0,0,0,0,gtk_entry_get_text(GTK_ENTRY(p.entry_b)));
  config_set_integer("mainpanel_loop",0,0,0,0,0,
		     gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p.cue_b)));

  for(i=0;i<input_ch || i<OUTPUT_CHANNELS;i++)
    config_set_integer("mainpanel_VU_show",0,0,0,0,i,
		       gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p.channelshow[i])));

  clippanel_state_to_config(bank);
  compandpanel_state_to_config(bank);
  singlepanel_state_to_config(bank);
  deverbpanel_state_to_config(bank);
  eqpanel_state_to_config(bank);
  reverbpanel_state_to_config(bank);
  limitpanel_state_to_config(bank);
  outpanel_state_to_config(bank);
  mixpanel_state_to_config(bank);

}

static void mainpanel_state_from_config(int bank){

  int val,i;
  const char *string;

  if(!config_get_integer("mainpanel_master_att",bank,0,0,0,0,&val))
    multibar_thumb_set(MULTIBAR(p.masterdB_s),val*.1,0);

  if(!config_get_integer("mainpanel_master_att_active",bank,0,0,0,0,&val))
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p.masterdB_a),val);

  /* A/B state are saved but *not* banked */
  if((string=config_get_string("mainpanel_cue_A",0,0,0,0)))
    action_seta_to(&p,string);
  if((string=config_get_string("mainpanel_cue_B",0,0,0,0)))
    action_setb_to(&p,string);
  if(!config_get_integer("mainpanel_loop",0,0,0,0,0,&val))
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p.cue_b),val);

  for(i=0;i<input_ch || i<OUTPUT_CHANNELS;i++)
    if(!config_get_integer("mainpanel_VU_show",0,0,0,0,i,&val))
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p.channelshow[i]),val);

  clippanel_state_from_config(bank);
  compandpanel_state_from_config(bank);
  singlepanel_state_from_config(bank);
  deverbpanel_state_from_config(bank);
  eqpanel_state_from_config(bank);
  reverbpanel_state_from_config(bank);
  limitpanel_state_from_config(bank);
  outpanel_state_from_config(bank);
  mixpanel_state_from_config(bank);

}

void save_state(){
  mainpanel_state_to_config(0);
  config_save(configfile);
}

static void savestatei(GtkWidget *dummy, gpointer in){
  save_state();
}

static void meterhold_reset(postfish_mainpanel *p){
  p->inpeak=-200;
  p->outpeak=-200;
  
  readout_set(READOUT(p->inreadout),"------");
  readout_set(READOUT(p->outreadout),"------");
}

static void action_zero(GtkWidget *widget,postfish_mainpanel *p){
  char buf[14];
  off_t cursor;

  output_halt_playback();
  cursor=input_seek(0);
  input_cursor_to_time(cursor,buf);
  readout_set(READOUT(p->cue),buf);
  multibar_reset(MULTIBAR(p->inbar));
  multibar_reset(MULTIBAR(p->outbar));
  clippanel_reset();
  eqpanel_reset();
  compandpanel_reset();
  singlepanel_reset();
  limitpanel_reset();
  mixpanel_reset();
  meterhold_reset(p);
}

static void action_end(GtkWidget *widget,postfish_mainpanel *p){
  char buf[14];
  off_t cursor=input_time_to_cursor("9999:59:59.99");

  output_halt_playback();
  cursor=input_seek(cursor);
  input_cursor_to_time(cursor,buf);
  readout_set(READOUT(p->cue),(char *)buf);
  multibar_reset(MULTIBAR(p->inbar));
  multibar_reset(MULTIBAR(p->outbar));
  clippanel_reset();
  eqpanel_reset();
  compandpanel_reset();
  singlepanel_reset();
  limitpanel_reset();
  mixpanel_reset();
  meterhold_reset(p);
}

static void action_bb(GtkWidget *widget,postfish_mainpanel *p){
  off_t cursor=input_time_seek_rel(-60);
  char time[14];
  input_cursor_to_time(cursor,time);
  readout_set(READOUT(p->cue),(char *)time);
}

static void action_b(GtkWidget *widget,postfish_mainpanel *p){
  off_t cursor=input_time_seek_rel(-5);
  char time[14];
  input_cursor_to_time(cursor,time);
  readout_set(READOUT(p->cue),(char *)time);
}

static void action_f(GtkWidget *widget,postfish_mainpanel *p){
  off_t cursor=input_time_seek_rel(5);
  char time[14];
  input_cursor_to_time(cursor,time);
  readout_set(READOUT(p->cue),(char *)time);
}

static void action_ff(GtkWidget *widget,postfish_mainpanel *p){
  off_t cursor=input_time_seek_rel(60);
  char time[14];
  input_cursor_to_time(cursor,time);
  readout_set(READOUT(p->cue),(char *)time);
}

/* gotta have the Fucking Fish */
static int reanimate_fish(postfish_mainpanel *p){
  if(playback_active || (p->fishframe>0 && p->fishframe<12)){
    /* continue spinning */
    p->fishframe++;
    if(p->fishframe>=12)p->fishframe=0;

    gtk_image_set_from_pixmap(GTK_IMAGE(p->twirlimage),
			      p->ff[p->fishframe],
			      p->fb[p->fishframe]);

    if(p->fishframe==0 && !playback_active){
      /* reschedule to blink */
      p->fishframe_timer=
	g_timeout_add(rand()%1000*30,(GSourceFunc)reanimate_fish,p);
      return FALSE;
    }
  }else{
    p->fishframe++;
    if(p->fishframe<=1)p->fishframe=12;
    if(p->fishframe>=19)p->fishframe=0;

    gtk_image_set_from_pixmap(GTK_IMAGE(p->twirlimage),
			      p->ff[p->fishframe],
			      p->fb[p->fishframe]);


    if(p->fishframe==12){
      /* reschedule to animate */
      p->fishframe_timer=
	g_timeout_add(10,(GSourceFunc)reanimate_fish,p);
      return FALSE;
    }
    if(p->fishframe==0){
      /* reschedule to blink */
      p->fishframe_timer=
	g_timeout_add(rand()%1000*30,(GSourceFunc)reanimate_fish,p);
      return FALSE;
    }
  }
  return TRUE;
}

static void animate_fish(postfish_mainpanel *p){
  if(p->fishframe_init){
    g_source_remove(p->fishframe_timer);
    p->fishframe_timer=
      g_timeout_add(80,(GSourceFunc)reanimate_fish,p);
  }else{
    p->fishframe_init=1;
    p->fishframe_timer=
      g_timeout_add(rand()%1000*30,(GSourceFunc)reanimate_fish,p);
  }
}

static void action_play(GtkWidget *widget,postfish_mainpanel *p){
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))){
    if(!playback_active){
      pthread_t playback_thread_id;
      playback_active=1;
      meterhold_reset(p);
      animate_fish(p);
      pthread_create(&playback_thread_id,NULL,&playback_thread,NULL);
    }
  }else{
    output_halt_playback();
  }
}

static void action_entrya(GtkWidget *widget,postfish_mainpanel *p){
  const char *time=gtk_entry_get_text(GTK_ENTRY(p->entry_a));
  off_t cursor=input_time_to_cursor(time);
  
  input_seek(cursor);
  readout_set(READOUT(p->cue),(char *)time);

}

static void action_entryb(GtkWidget *widget,postfish_mainpanel *p){
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))){

    const char *time=gtk_entry_get_text(GTK_ENTRY(p->entry_b));
    off_t cursora,cursorb=input_time_to_cursor(time);
    time=gtk_entry_get_text(GTK_ENTRY(p->entry_a));
    cursora=input_time_to_cursor(time);
    
    if(cursora<cursorb){
      input_Acursor_set(cursora);
      input_Bcursor_set(cursorb);
      loop_active=1;
    }else{
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),0);
    }
  }else
    loop_active=0;
  
  gtk_image_set_from_pixmap(GTK_IMAGE(p->playimage),
			    p->pf[loop_active],
			    p->pb[loop_active]);
    
}

static void action_seta_to(postfish_mainpanel *p,const char *time){
  gtk_entry_set_text(GTK_ENTRY(p->entry_a),time);

  {
    const char *time=gtk_entry_get_text(GTK_ENTRY(p->entry_a));
    off_t cursora=input_time_to_cursor(time),cursorb;
    time=gtk_entry_get_text(GTK_ENTRY(p->entry_b));
    cursorb=input_time_to_cursor(time);
    
    if(cursora>=cursorb && loop_active){
      loop_active=0;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->cue_b),0);
    }
    input_Acursor_set(cursora);
  }

}

static void action_seta(GtkWidget *widget,postfish_mainpanel *p){
  const char *time=readout_get(READOUT(p->cue));
  action_seta_to(p,time);
}

static void action_setb_to(postfish_mainpanel *p,const char *time){
  off_t cursora,cursorb;
  
  cursorb=input_time_to_cursor(time);
  gtk_entry_set_text(GTK_ENTRY(p->entry_b),time);
    
  time=gtk_entry_get_text(GTK_ENTRY(p->entry_a));
  cursora=input_time_to_cursor(time);

  if(cursora>=cursorb && loop_active){
    loop_active=0;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->cue_b),0);
  }
  input_Bcursor_set(cursorb);
 
}

static void action_setb(GtkWidget *widget,postfish_mainpanel *p){
  const char *time=  time=readout_get(READOUT(p->cue));
  action_setb_to(p,time);
}

static void shutdown(void){
  output_halt_playback();
  save_state();
  main_looping=0;
  gtk_main_quit();
}

static void masterdB_change(GtkWidget *dummy, gpointer in){
  postfish_mainpanel *p=in;
  char buf[80];
  float val=multibar_get_value(MULTIBAR(p->masterdB_s),0);
  sprintf(buf,"%+5.1fdB",val);
  readout_set(READOUT(p->masterdB_r),buf);

  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->masterdB_a)))
    master_att=rint(val*10);
  else
    master_att=0;
}

static gboolean timeevent_unselect(GtkWidget *widget,
				   gpointer dummy){

  gtk_editable_select_region(GTK_EDITABLE(widget),0,0);
  return FALSE;
}

static gboolean channel_keybinding(GtkWidget *widget,
				   GdkEventKey *event,
				   gpointer in){
  postfish_mainpanel *p=in;
  int keych=event->keyval-GDK_0;
  int actualch,widgetnum;
  
  if(keych==0)
    keych=9;
  else 
    keych--;

  if(keych<0 || keych>9)return FALSE;
  actualch=keych;

  widgetnum=p->channelrotate[keych];

  while(widgetnum>=p->channeleffects){
    widgetnum-=p->channeleffects;
    actualch+=10;
  }

  gtk_widget_grab_focus(p->channel_wa[widgetnum][actualch]);
  
  widgetnum=++p->channelrotate[keych];
  
  actualch=keych;
  while(widgetnum>=p->channeleffects){
    widgetnum-=p->channeleffects;
    actualch+=10;
  }
  if(actualch>=input_ch)p->channelrotate[keych]=0;

  return TRUE;
}

static gboolean timeevent_keybinding(GtkWidget *widget,
				     GdkEventKey *event,
				     gpointer in){
  GtkWidget *toplevel=GTK_WIDGET(in);
  GtkEntry *e=GTK_ENTRY(widget);
  gint pos=gtk_editable_get_position(GTK_EDITABLE(widget));
  const gchar *text=gtk_entry_get_text(e);

  /* we accept only numerals, forward arrow keys, Enter, tab, alt-tab,
     block all else */
  switch(event->keyval){
  case GDK_6:
  case GDK_7:
  case GDK_8:
  case GDK_9:
    if(pos==5 || pos==8)return TRUE;
  case GDK_0:
  case GDK_1:
  case GDK_2:
  case GDK_3:
  case GDK_4:
  case GDK_5:
    if(pos==4 || pos==7 || pos==10)pos++;

    if(pos>12)return TRUE;
    {
      char buffer[15];
      strncpy(buffer,text,15);
      if(pos>13)pos=13;
      buffer[pos]=event->keyval;

      time_fix(buffer);

      pos++;
      if(pos==4 || pos==7 || pos==10)pos++;
      gtk_entry_set_text(e,buffer);
      gtk_editable_set_position(GTK_EDITABLE(widget),pos);
      return TRUE;
    }
    break;
  case GDK_BackSpace:
    /* rewrite to left arrow */
  case GDK_Left:
    if(pos==0){
      /* back up focus */
      gtk_widget_child_focus(toplevel,GTK_DIR_LEFT);
      return TRUE;
    }

    pos--;
    if(pos==4 || pos==7 || pos==10)pos--;
    gtk_editable_set_position(GTK_EDITABLE(widget),pos);
    return  TRUE;

  case GDK_Right:
    if(pos>=12){
      /* advance focus */
      gtk_widget_child_focus(toplevel,GTK_DIR_RIGHT);
      return TRUE;
    }

    pos++;
    if(pos==4 || pos==7 || pos==10)pos++;
    gtk_editable_set_position(GTK_EDITABLE(widget),pos);
    return  TRUE;

  case GDK_Tab:
  case GDK_ISO_Left_Tab:
  case GDK_Up:
  case GDK_Down:
    return FALSE;

  default:
    return TRUE;
  }
}

static gboolean mainpanel_keybinding(GtkWidget *widget,
			      GdkEventKey *event,
			      gpointer in){
  postfish_mainpanel *p=in;


#if 0
  fprintf(stderr,"keypress: M%d C%d S%d L%d '%x'\n",
	  event->state&GDK_MOD1_MASK,
	  event->state&GDK_CONTROL_MASK,
	  event->state&GDK_SHIFT_MASK,
	  event->state&GDK_LOCK_MASK,
	  event->keyval);
#endif

  if(event->state&GDK_MOD1_MASK) return FALSE;
  if(event->state&GDK_CONTROL_MASK)return FALSE;

  /* non-control keypresses */
  switch(event->keyval){
  case GDK_less:
  case GDK_comma:
  case GDK_period:
  case GDK_greater:
    
    /* GTK has one unfortunate bit of hardwiring; if a key is held down
       and autorepeats, the default pushbutton widget-unactivate timeout
       is 250 ms, far slower than autorepeat.  We must defeat this to
       have autorepeat accellerators function at full speed. */
    
    {
      GdkEventKey copy=*event;
      copy.type=GDK_KEY_RELEASE;
      gtk_main_do_event((GdkEvent *)(&copy));
    }
    while (gtk_events_pending ())  gtk_main_iteration ();
  }
  
  
  switch(event->keyval){
  case GDK_minus:
    multibar_thumb_set(MULTIBAR(p->masterdB_s),
		       multibar_get_value(MULTIBAR(p->masterdB_s),0)-.1,0);
    break;
  case GDK_underscore:
    multibar_thumb_set(MULTIBAR(p->masterdB_s),
		       multibar_get_value(MULTIBAR(p->masterdB_s),0)-1.,0);
    break;
  case GDK_equal:
    multibar_thumb_set(MULTIBAR(p->masterdB_s),
			 multibar_get_value(MULTIBAR(p->masterdB_s),0)+.1,0);
    break;
  case GDK_plus:
    multibar_thumb_set(MULTIBAR(p->masterdB_s),
		       multibar_get_value(MULTIBAR(p->masterdB_s),0)+1.,0);
    break;
  default:
    return FALSE;
  }

  return TRUE;
}

static void mainpanel_chentry(postfish_mainpanel *p,
			      GtkWidget *table,
			      char *label,
			      int i,
			      void (*single_create)
			      (postfish_mainpanel *,
			       GtkWidget **, GtkWidget **),
			      void (*multi_create)
			      (postfish_mainpanel *,
			       GtkWidget **, GtkWidget **)){  
  
  int j;
  GtkWidget *wm[input_ch+1];
  GtkWidget *wa[input_ch];
  
  for(j=0;j<input_ch;j++){
    char buffer[80];
    sprintf(buffer,"  %d ",j+1);
    p->channel_wa[i][j]=wa[j]=gtk_toggle_button_new_with_label(buffer);

    if(multi_create){
      /* a panel button per channel, multiple accellerated labels */
      GtkWidget *l=gtk_label_new_with_mnemonic(label);
      GtkWidget *a=gtk_alignment_new(0,.5,0,0);
      gtk_widget_set_name(l,"windowbuttonlike");
      gtk_misc_set_alignment(GTK_MISC(l),0,.5);
      wm[j]=windowbutton_new(NULL);
      gtk_container_add(GTK_CONTAINER(a),wm[j]);
      gtk_table_attach_defaults(GTK_TABLE(table),a,2+j*2,4+j*2,i+1,i+2);
      gtk_table_attach(GTK_TABLE(table),l,1,2,i+1,i+2,GTK_FILL,0,0,0);
      if(j>0 || single_create)gtk_widget_set_size_request(l,0,0);
      gtk_label_set_mnemonic_widget(GTK_LABEL(l),wm[j]);

      {
	GtkRequisition requisition;
	GtkWidget *label=gtk_label_new(NULL);
	gtk_widget_size_request(wm[j],&requisition);
	gtk_widget_set_size_request(label,requisition.width/2,1);
	gtk_table_attach_defaults(GTK_TABLE(table),label,2+j*2,3+j*2,i+1,i+2);
	gtk_table_attach_defaults(GTK_TABLE(table),wa[j],3+j*2,4+j*2,i+1,i+2);

      }
    }else{
      gtk_table_attach_defaults(GTK_TABLE(table),wa[j],3+j*2,4+j*2,i+1,i+2);
    }
  }

  if(single_create){
    /* one master windowbutton, one label */
    wm[input_ch]=windowbutton_new(label);
    gtk_table_attach_defaults(GTK_TABLE(table),wm[input_ch],0,2,i+1,i+2);

    (*single_create)(p,wm+input_ch,wa);
  }else{
    GtkWidget *b=windowbutton_new(NULL);
    
    gtk_widget_set_sensitive(b,FALSE);
    gtk_table_attach_defaults(GTK_TABLE(table),b,0,1,i+1,i+2);
  }

  if(multi_create)
    (*multi_create)(p,wm,wa);
  
  if (!single_create && !multi_create){  
    GtkWidget *l=gtk_label_new(label);
    
    gtk_widget_set_name(l,"windowbuttonlike");
    gtk_misc_set_alignment(GTK_MISC(l),0,.5);
    gtk_table_attach_defaults(GTK_TABLE(table),l,1,2,i+1,i+2);
  }

}

static void mainpanel_masterentry(postfish_mainpanel *p,
				  GtkWidget *table,
				  char *label,
				  char *shortcut,
				  guint key,
				  int i,
				  void (*panel_create)
				  (postfish_mainpanel *,
				   GtkWidget *, GtkWidget *)){  
  
  GtkWidget *ww=windowbutton_new(label);
  GtkWidget *wa=(shortcut?
		 gtk_toggle_button_new_with_label(shortcut):
		 gtk_frame_new(shortcut));
  
  gtk_table_attach_defaults(GTK_TABLE(table),ww,0,1,i+1,i+2);
  gtk_table_attach_defaults(GTK_TABLE(table),wa,1,2,i+1,i+2);
  if(shortcut)
    gtk_widget_add_accelerator (wa, "activate", p->group, key, 0, 0);
  else{
    gtk_frame_set_shadow_type(GTK_FRAME(wa),GTK_SHADOW_ETCHED_IN);
  }

  if(panel_create)(*panel_create)(p,ww,(shortcut?wa:0));
}

#define CHANNEL_EFFECTS 7
#define MASTER_EFFECTS 7
extern char *version;
void mainpanel_create(postfish_mainpanel *panel,char **chlabels){
  char *text_bar[7]={"[bksp]","[<]","[,]","[space]","[.]","[>]","[end]"};

  int i;
  GdkPixmap *xpm_bar[7];
  GdkBitmap *xbm_bar[7];
  GtkWidget *gim_bar[7];

  GtkWidget *topplace,*topal,*topalb;

  GtkWidget *topframe=gtk_frame_new (NULL);
  GtkWidget *toplabel=gtk_label_new (NULL);

  GtkWidget *quitbutton=gtk_button_new_with_mnemonic("_quit");
  GtkWidget *savebutton=gtk_button_new_with_label("^save");

  GtkWidget *mainbox=gtk_hbox_new(0,6);
  GtkWidget *masterback=gtk_event_box_new();
  GtkWidget *channelback=gtk_event_box_new();
  GtkWidget *masterframe=gtk_frame_new(" Master ");
  GtkWidget *channelframe=gtk_frame_new(" Channel ");

  GtkWidget *deckbox=gtk_vbox_new(0,0);
  GtkWidget *channelbox=gtk_vbox_new(0,0);
  GtkWidget *masterbox=gtk_vbox_new(0,6);

  GtkWidget *channeltable=gtk_table_new(CHANNEL_EFFECTS+1,input_ch*2+2,0);
  GtkWidget *mastertable=gtk_table_new(MASTER_EFFECTS+1,2,0);

  GdkWindow *root=gdk_get_default_root_window();

  panel->toplevel=gtk_window_new (GTK_WINDOW_TOPLEVEL);
  panel->group = gtk_accel_group_new ();
  gtk_window_add_accel_group (GTK_WINDOW(panel->toplevel), panel->group);

  panel->channel_wa=malloc(CHANNEL_EFFECTS*sizeof(*panel->channel_wa));
  for(i=0;i<CHANNEL_EFFECTS;i++)
    panel->channel_wa[i]=malloc(input_ch*sizeof(**panel->channel_wa));
  panel->channeleffects=CHANNEL_EFFECTS;
  
  char versionmarkup[240];
  snprintf(versionmarkup,240," <span size=\"large\" weight=\"bold\" "
	   "style=\"italic\" foreground=\"dark blue\">"
	   "Postfish</span>  <span size=\"small\" foreground=\"#606060\">"
	   "revision %s</span> ",
	   version);


  topplace=gtk_table_new(1,1,0);
  topalb=gtk_hbox_new(0,0);
  topal=gtk_alignment_new(1,0,0,0);

  gtk_widget_set_name(quitbutton,"quitbutton");

  gtk_box_pack_start(GTK_BOX(topalb),savebutton,0,0,0);
  gtk_box_pack_start(GTK_BOX(topalb),quitbutton,0,0,0);
  gtk_container_add (GTK_CONTAINER(topal),topalb);

  
  gtk_table_attach_defaults(GTK_TABLE(topplace),
			    topal,0,1,0,1);
  gtk_table_attach_defaults(GTK_TABLE(topplace),
			    topframe,0,1,0,1);
    
  gtk_container_add (GTK_CONTAINER (panel->toplevel), topplace);
  gtk_container_set_border_width (GTK_CONTAINER (quitbutton), 3);
  gtk_container_set_border_width (GTK_CONTAINER (savebutton), 3);

  g_signal_connect (G_OBJECT (quitbutton), "clicked",
		    G_CALLBACK (shutdown), NULL);

  g_signal_connect (G_OBJECT (savebutton), "clicked",
		    G_CALLBACK (savestatei), 0);
  
  gtk_widget_add_accelerator (savebutton, "activate", panel->group, GDK_s, GDK_CONTROL_MASK, 0);


  g_signal_connect (G_OBJECT (panel->toplevel), "key-press-event",
		    G_CALLBACK (mainpanel_keybinding), panel);

  g_signal_connect_after (G_OBJECT (panel->toplevel), "key-press-event",
			  G_CALLBACK (channel_keybinding), panel);
  

  for(i=0;i<19;i++)
    panel->ff[i]=gdk_pixmap_create_from_xpm_d(root,
					      panel->fb+i,NULL,ff_xpm[i]);

  xpm_bar[0]=gdk_pixmap_create_from_xpm_d(root,
					  xbm_bar,NULL,bar_home_xpm);
  xpm_bar[1]=gdk_pixmap_create_from_xpm_d(root,
					  xbm_bar+1,NULL,bar_bb_xpm);
  xpm_bar[2]=gdk_pixmap_create_from_xpm_d(root,
					  xbm_bar+2,NULL,bar_b_xpm);
  xpm_bar[3]=gdk_pixmap_create_from_xpm_d(root,
					  xbm_bar+3,NULL,bar_p_xpm);
  xpm_bar[4]=gdk_pixmap_create_from_xpm_d(root,
					  xbm_bar+4,NULL,bar_f_xpm);
  xpm_bar[5]=gdk_pixmap_create_from_xpm_d(root,
					  xbm_bar+5,NULL,bar_ff_xpm);
  xpm_bar[6]=gdk_pixmap_create_from_xpm_d(root,
					  xbm_bar+6,NULL,bar_end_xpm);


  panel->pf[0]=xpm_bar[3];
  panel->pb[0]=xbm_bar[3];
  panel->pf[1]=gdk_pixmap_create_from_xpm_d(root,
					    panel->pb+1,NULL,bar_l_xpm);

  for(i=0;i<7;i++)
    gim_bar[i]=gtk_image_new_from_pixmap(xpm_bar[i],xbm_bar[i]);
  panel->playimage=gim_bar[3];

  panel->twirlimage=gtk_image_new_from_pixmap(panel->ff[0],panel->fb[0]);

  gtk_container_set_border_width (GTK_CONTAINER (topframe), 3);
  gtk_container_set_border_width (GTK_CONTAINER (mainbox), 3);
  gtk_container_set_border_width (GTK_CONTAINER (masterbox), 3);
  gtk_container_set_border_width (GTK_CONTAINER (channelbox), 3);
  gtk_frame_set_shadow_type(GTK_FRAME(topframe),GTK_SHADOW_ETCHED_IN);
  gtk_frame_set_label_widget(GTK_FRAME(topframe),toplabel);
  gtk_label_set_markup(GTK_LABEL(toplabel),versionmarkup);

  gtk_container_add (GTK_CONTAINER(topframe), mainbox);
  gtk_box_pack_end(GTK_BOX(mainbox),masterbox,0,0,0);
  gtk_box_pack_end(GTK_BOX(mainbox),channelbox,0,0,0);
  gtk_box_pack_start(GTK_BOX(masterbox),masterback,1,1,0);
  gtk_box_pack_start(GTK_BOX(channelbox),channelback,1,1,0);
  gtk_container_add (GTK_CONTAINER(masterback), masterframe);
  gtk_container_add (GTK_CONTAINER(channelback), channelframe);
  gtk_box_pack_start(GTK_BOX(mainbox),deckbox,0,0,0);
  gtk_widget_set_name(masterback,"winpanel");
  gtk_widget_set_name(channelback,"winpanel");

  gtk_frame_set_label_align(GTK_FRAME(channelframe),.5,0.);
  gtk_frame_set_label_align(GTK_FRAME(masterframe),.5,0.);

  /* left side of main panel */
  {
    char *labels[13]={"","-96","-72","-60","-48","-36","-24",
		      "-16","-8","-3","0","+3","+6"};
    float levels[13]={-140.,-96.,-72.,-60.,-48.,-36.,-24.,
		       -16.,-8.,-3.,0.,+3.,+6.};

    GtkWidget *ttable=gtk_table_new(7,2,0);
    GtkWidget *togglebox=gtk_hbox_new(0,0);
    GtkWidget *in=gtk_label_new("in:");
    GtkWidget *out=gtk_label_new("out:");
    GtkWidget *show=gtk_label_new("show:");
    GtkWidget *inbox=gtk_hbox_new(0,0);
    GtkWidget *outbox=gtk_hbox_new(0,0);

    panel->inbar=multibar_new(13,labels,levels, 0,
			      LO_ATTACK|LO_DECAY|HI_DECAY|PEAK_FOLLOW );
    panel->outbar=multibar_new(13,labels,levels, 0,
			       LO_ATTACK|LO_DECAY|HI_DECAY|PEAK_FOLLOW );
    panel->inreadout=readout_new("------");
    panel->outreadout=readout_new("------");

    gtk_widget_set_name(panel->inreadout,"smallreadout");
    gtk_widget_set_name(panel->outreadout,"smallreadout");
    
    gtk_box_pack_start(GTK_BOX(inbox),panel->inbar,1,1,0);
    gtk_box_pack_end(GTK_BOX(inbox),panel->inreadout,0,1,0);
    gtk_box_pack_start(GTK_BOX(outbox),panel->outbar,1,1,0);
    gtk_box_pack_end(GTK_BOX(outbox),panel->outreadout,0,1,0);

    gtk_container_set_border_width(GTK_CONTAINER (ttable), 3);
    gtk_table_set_col_spacings(GTK_TABLE(ttable),5);
    gtk_misc_set_alignment(GTK_MISC(show),1,.5);
    gtk_misc_set_alignment(GTK_MISC(in),1,.5);
    gtk_misc_set_alignment(GTK_MISC(out),1,.5);
    gtk_box_set_spacing(GTK_BOX(togglebox),5);

    for(i=0;;i++){
      if(!chlabels[i])break;

      GtkWidget *button=gtk_check_button_new_with_mnemonic(chlabels[i]);
      if(i<input_ch)gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button),TRUE);
      gtk_box_pack_start(GTK_BOX(togglebox),button,0,0,0);
      {
	char buffer[]="color\0\0";
	buffer[5]= (i%10)+48;
	gtk_widget_set_name(button,buffer);
      }
      panel->channelshow[i]=button;
    }

    gtk_table_attach_defaults(GTK_TABLE(ttable),togglebox,1,3,0,1);
    gtk_table_attach(GTK_TABLE(ttable),show,0,1,0,1,GTK_FILL|GTK_SHRINK,GTK_FILL|GTK_SHRINK,0,0);
    gtk_table_attach(GTK_TABLE(ttable),in,0,1,1,2,GTK_FILL|GTK_SHRINK,GTK_FILL|GTK_SHRINK,0,0);
    gtk_table_attach(GTK_TABLE(ttable),out,0,1,2,3,GTK_FILL|GTK_SHRINK,GTK_FILL|GTK_SHRINK,0,0);
    gtk_table_attach(GTK_TABLE(ttable),inbox,1,3,1,2,GTK_EXPAND|GTK_FILL,GTK_EXPAND|GTK_FILL,0,0);
    gtk_table_attach(GTK_TABLE(ttable),outbox,1,3,2,3,GTK_EXPAND|GTK_FILL,GTK_EXPAND|GTK_FILL,0,0);

    gtk_table_set_row_spacing(GTK_TABLE(ttable),1,1);
    gtk_table_set_row_spacing(GTK_TABLE(ttable),2,2);

    /* master dB slider */
    {
      char *sliderlabels[11]={"","-40","-30","-20","-10","0","+10","+20","+30","+40","+50"};
      float sliderlevels[11]={-50,-40,-30,-20,-10,0,10,20,30,40,50};
      
      GtkWidget *box=gtk_hbox_new(0,0);

      GtkWidget *masterlabel=gtk_label_new("master:");
      panel->masterdB_a=gtk_toggle_button_new_with_label("a[t]ten");
      panel->masterdB_r=readout_new("  0.0dB");
      panel->masterdB_s=multibar_slider_new(11,sliderlabels,sliderlevels,1);
      
      multibar_thumb_set(MULTIBAR(panel->masterdB_s),0.,0);
      multibar_thumb_increment(MULTIBAR(panel->masterdB_s),.1,1.);

      gtk_misc_set_alignment(GTK_MISC(masterlabel),1,.5);
    
      gtk_table_attach(GTK_TABLE(ttable),masterlabel,0,1,3,4,
		       GTK_FILL,GTK_FILL,0,0);
      
      gtk_box_pack_start(GTK_BOX(box),panel->masterdB_s,1,1,0);
      gtk_box_pack_start(GTK_BOX(box),panel->masterdB_r,0,0,0);
      gtk_box_pack_start(GTK_BOX(box),panel->masterdB_a,0,0,0);
      
      gtk_table_attach_defaults(GTK_TABLE(ttable),box,1,3,3,4);

      multibar_callback(MULTIBAR(panel->masterdB_s),
			masterdB_change,(gpointer)panel);
      
      gtk_widget_add_accelerator (panel->masterdB_a, "activate", panel->group, GDK_t, 0, 0);
      g_signal_connect_after (G_OBJECT(panel->masterdB_a), "clicked",
			G_CALLBACK(masterdB_change), (gpointer)panel);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->masterdB_a),1);
    }

    /* master action bar */
    {
      GtkWidget *bar_table=gtk_table_new(1,8,1);
      for(i=0;i<7;i++){
	GtkWidget *box=gtk_vbox_new(0,3);
	GtkWidget *label=gtk_label_new(text_bar[i]);

	if(i==3)
	  panel->deckactive[i]=gtk_toggle_button_new();
	else
	  panel->deckactive[i]=gtk_button_new();

	gtk_box_pack_start(GTK_BOX(box),gim_bar[i],0,0,0);
	gtk_box_pack_start(GTK_BOX(box),label,0,0,0);
	gtk_container_add (GTK_CONTAINER(panel->deckactive[i]), box);
      }

      gtk_table_attach_defaults(GTK_TABLE(bar_table),panel->deckactive[0],0,1,0,1);
      gtk_table_attach_defaults(GTK_TABLE(bar_table),panel->deckactive[1],1,2,0,1);
      gtk_table_attach_defaults(GTK_TABLE(bar_table),panel->deckactive[2],2,3,0,1);
      gtk_table_attach_defaults(GTK_TABLE(bar_table),panel->deckactive[3],3,5,0,1);
      gtk_table_attach_defaults(GTK_TABLE(bar_table),panel->deckactive[4],5,6,0,1);
      gtk_table_attach_defaults(GTK_TABLE(bar_table),panel->deckactive[5],6,7,0,1);
      gtk_table_attach_defaults(GTK_TABLE(bar_table),panel->deckactive[6],7,8,0,1);

      gtk_table_attach(GTK_TABLE(ttable),bar_table,1,3,4,5,
		       GTK_EXPAND|GTK_FILL,GTK_EXPAND|GTK_FILL,
		       0,8);

      gtk_table_attach(GTK_TABLE(ttable),panel->twirlimage,0,1,4,5,
      		       0,0,
		       0,0);


      g_signal_connect (G_OBJECT (panel->deckactive[0]), "clicked",
			G_CALLBACK (action_zero), panel);
      g_signal_connect (G_OBJECT (panel->deckactive[1]), "clicked",
			G_CALLBACK (action_bb), panel);
      g_signal_connect (G_OBJECT (panel->deckactive[2]), "clicked",
			G_CALLBACK (action_b), panel);
      g_signal_connect (G_OBJECT (panel->deckactive[3]), "clicked",
			G_CALLBACK (action_play), panel);
      g_signal_connect (G_OBJECT (panel->deckactive[4]), "clicked",
			G_CALLBACK (action_f), panel);
      g_signal_connect (G_OBJECT (panel->deckactive[5]), "clicked",
			G_CALLBACK (action_ff), panel);
      g_signal_connect (G_OBJECT (panel->deckactive[6]), "clicked",
			G_CALLBACK (action_end), panel);
      
      gtk_widget_add_accelerator (panel->deckactive[3], "activate", panel->group, GDK_space, 0, 0);

      if(!input_seekable){
	gtk_widget_set_sensitive(panel->deckactive[0],FALSE);
	gtk_widget_set_sensitive(panel->deckactive[1],FALSE);
	gtk_widget_set_sensitive(panel->deckactive[2],FALSE);
	gtk_widget_set_sensitive(panel->deckactive[4],FALSE);
	gtk_widget_set_sensitive(panel->deckactive[5],FALSE);
	gtk_widget_set_sensitive(panel->deckactive[6],FALSE);
      }else{
	gtk_widget_add_accelerator (panel->deckactive[0], "activate", panel->group, GDK_BackSpace, 0, 0);
	gtk_widget_add_accelerator (panel->deckactive[1], "activate", panel->group, GDK_less, 0, 0);
	gtk_widget_add_accelerator (panel->deckactive[2], "activate", panel->group, GDK_comma, 0, 0);
	gtk_widget_add_accelerator (panel->deckactive[4], "activate", panel->group, GDK_period, 0, 0);
	gtk_widget_add_accelerator (panel->deckactive[5], "activate", panel->group, GDK_greater, 0, 0);
	gtk_widget_add_accelerator (panel->deckactive[6], "activate", panel->group, GDK_End, 0, 0);
      }

    }

    /* cue bar */
    {
      GtkWidget *cuebox=gtk_hbox_new(0,0);
      GtkWidget *cuelabel=gtk_label_new("cue:");

      GtkWidget *framea=gtk_vseparator_new();
      GtkWidget *frameb=gtk_vseparator_new();

      GtkWidget *panelb=windowbutton_new("c_ue list");
      GtkWidget *temp;

      panel->entry_a=gtk_entry_new();
      panel->entry_b=gtk_entry_new();

      panel->cue=readout_new("    :  :00.00");

      temp=gtk_button_new_with_label(" a ");
      gtk_widget_add_accelerator (temp, "activate", panel->group, GDK_a, 0, 0);
      g_signal_connect (G_OBJECT (temp), "clicked",
			G_CALLBACK (action_entrya), panel);
      gtk_box_pack_start(GTK_BOX(cuebox),panel->cue,0,0,0);
      gtk_box_pack_start(GTK_BOX(cuebox),framea,1,1,3);
      gtk_box_pack_start(GTK_BOX(cuebox),temp,0,0,0);
      gtk_box_pack_start(GTK_BOX(cuebox),panel->entry_a,0,0,0);
      if(!input_seekable)gtk_widget_set_sensitive(temp,FALSE);

      temp=gtk_button_new_with_label(" A ");
      gtk_widget_add_accelerator (temp, "activate", panel->group, GDK_A, GDK_SHIFT_MASK, 0);
      g_signal_connect (G_OBJECT (temp), "clicked",
			G_CALLBACK (action_seta), panel);
      gtk_box_pack_start(GTK_BOX(cuebox),temp,0,0,0);
      
      panel->cue_b=temp=gtk_toggle_button_new_with_label(" b ");
      gtk_widget_add_accelerator (temp, "activate", panel->group, GDK_b, 0, 0);
      g_signal_connect (G_OBJECT (temp), "clicked",
			G_CALLBACK (action_entryb), panel);
      gtk_box_pack_start(GTK_BOX(cuebox),frameb,1,1,3);
      gtk_box_pack_start(GTK_BOX(cuebox),temp,0,0,0);
      if(!input_seekable)gtk_widget_set_sensitive(temp,FALSE);

      temp=gtk_button_new_with_label(" B ");
      gtk_widget_add_accelerator (temp, "activate", panel->group, GDK_B, GDK_SHIFT_MASK, 0);
      g_signal_connect (G_OBJECT (temp), "clicked",
			G_CALLBACK (action_setb), panel);
      gtk_box_pack_start(GTK_BOX(cuebox),panel->entry_b,0,0,0);
      gtk_box_pack_start(GTK_BOX(cuebox),temp,0,0,0);

      gtk_entry_set_width_chars(GTK_ENTRY(panel->entry_a),13);
      gtk_entry_set_width_chars(GTK_ENTRY(panel->entry_b),13);
      gtk_entry_set_text(GTK_ENTRY(panel->entry_a),"    :  :00.00");
      gtk_entry_set_text(GTK_ENTRY(panel->entry_b),"    :  :00.00");


      g_signal_connect (G_OBJECT (panel->entry_a), "key-press-event",
			G_CALLBACK (timeevent_keybinding), panel->toplevel);
      g_signal_connect (G_OBJECT (panel->entry_b), "key-press-event",
			G_CALLBACK (timeevent_keybinding), panel->toplevel);

      g_signal_connect_after(G_OBJECT (panel->entry_a), "grab_focus",
			G_CALLBACK (timeevent_unselect), NULL);
      g_signal_connect_after(G_OBJECT (panel->entry_b), "grab_focus",
			G_CALLBACK (timeevent_unselect), NULL);

      gtk_misc_set_alignment(GTK_MISC(cuelabel),1,.5);

      gtk_table_attach_defaults(GTK_TABLE(ttable),cuelabel,0,1,5,6);
      gtk_table_attach_defaults(GTK_TABLE(ttable),cuebox,1,2,5,6);
      gtk_table_attach_defaults(GTK_TABLE(ttable),panelb,2,3,5,6);

    }

    /* config bar */
    {
      GtkWidget *confbox=gtk_hbox_new(0,0);
      GtkWidget *conflabel=gtk_label_new("setting:");
      GtkWidget *conf=readout_new("");
      GtkWidget *panel=windowbutton_new("_setting list");
      gtk_misc_set_alignment(GTK_MISC(conflabel),1,.5);

      gtk_table_attach_defaults(GTK_TABLE(ttable),conflabel,0,1,6,7);
      gtk_table_attach(GTK_TABLE(ttable),confbox,1,2,6,7,
		       GTK_EXPAND|GTK_FILL,GTK_EXPAND|GTK_FILL,0,3);
      gtk_table_attach_defaults(GTK_TABLE(ttable),panel,2,3,6,7);

      gtk_box_pack_start(GTK_BOX(confbox),conf,1,1,0);
    }


    gtk_box_pack_end(GTK_BOX(deckbox),ttable,0,0,0);

  }


  /* channel panel */
  
  {
    GtkWidget *temp=gtk_label_new(NULL);
    GtkWidget *alignbox=gtk_vbox_new(0,0);
    gtk_container_set_border_width (GTK_CONTAINER (channeltable), 3);
    gtk_frame_set_shadow_type(GTK_FRAME(channelframe),GTK_SHADOW_ETCHED_IN);
    gtk_box_pack_start(GTK_BOX(alignbox),channeltable,0,0,0);
    gtk_container_add(GTK_CONTAINER(channelframe),alignbox);
    gtk_table_set_row_spacings(GTK_TABLE(channeltable),1);

    gtk_label_set_markup(GTK_LABEL(temp),"<span size=\"x-small\">visible</span>");
    gtk_misc_set_alignment(GTK_MISC(temp),0,.5);
    gtk_table_attach_defaults(GTK_TABLE(channeltable),temp,0,2,0,1);

    temp=gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(temp),"<span size=\"x-small\">active</span>");
    gtk_misc_set_alignment(GTK_MISC(temp),1,.5);
    gtk_table_attach_defaults(GTK_TABLE(channeltable),temp,1,2+input_ch*2,0,1);
  }

  mainpanel_chentry(panel,channeltable,"_Declip ",0,clippanel_create,0);
  mainpanel_chentry(panel,channeltable,"De_verb ",1,deverbpanel_create_channel,0);
  mainpanel_chentry(panel,channeltable,"_Multicomp ",2,0,compandpanel_create_channel);
  mainpanel_chentry(panel,channeltable,"_Singlecomp ",3,0,singlepanel_create_channel);
  mainpanel_chentry(panel,channeltable,"_EQ ",4,0,eqpanel_create_channel);
  mainpanel_chentry(panel,channeltable,"_Reverb ",5,0,reverbpanel_create_channel);
  mainpanel_chentry(panel,channeltable,"Atten/Mi_x ",6,attenpanel_create,
		    mixpanel_create_channel);

  /* master panel */
  {
    GtkWidget *temp=gtk_label_new(NULL);
    GtkWidget *alignbox=gtk_vbox_new(0,0);
    gtk_container_set_border_width (GTK_CONTAINER (mastertable), 3);
    gtk_frame_set_shadow_type(GTK_FRAME(masterframe),GTK_SHADOW_ETCHED_IN);
    gtk_box_pack_start(GTK_BOX(alignbox),mastertable,0,0,0);
    gtk_container_add(GTK_CONTAINER(masterframe),alignbox);
    gtk_table_set_row_spacings(GTK_TABLE(mastertable),1);


    gtk_label_set_markup(GTK_LABEL(temp),"<span size=\"x-small\">visible</span>");
    gtk_misc_set_alignment(GTK_MISC(temp),0,.5);
    gtk_table_attach_defaults(GTK_TABLE(mastertable),temp,0,1,0,1);

    temp=gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(temp),"<span size=\"x-small\">active</span>");
    gtk_misc_set_alignment(GTK_MISC(temp),1,.5);
    gtk_table_attach_defaults(GTK_TABLE(mastertable),temp,1,2,0,1);
  }

  mainpanel_masterentry(panel,mastertable,"_Multicomp "," m ",GDK_m,0,compandpanel_create_master);
  mainpanel_masterentry(panel,mastertable,"_Singlecomp "," s ",GDK_s,1,singlepanel_create_master);
  mainpanel_masterentry(panel,mastertable,"_EQ "," e ",GDK_e,2,eqpanel_create_master);
  mainpanel_masterentry(panel,mastertable,"_Reverb "," r ",GDK_r,3,reverbpanel_create_master);
  mainpanel_masterentry(panel,mastertable,"_Limit "," l ",GDK_l,4,limitpanel_create);

  /* output has three activity buttons not in the main grid */
  {
    GtkWidget *ww=windowbutton_new("_Output ");

    GtkWidget *std=gtk_toggle_button_new_with_label(" o ");
    GtkWidget *ply=gtk_toggle_button_new_with_label("mOn");
    GtkWidget *box=gtk_hbox_new(0,0);
    GtkWidget *box2=gtk_hbox_new(1,0);

    GtkWidget *fw=windowbutton_new(NULL);
    GtkWidget *fr=gtk_frame_new(NULL);
  
    gtk_frame_set_shadow_type(GTK_FRAME(fr),GTK_SHADOW_ETCHED_IN);
    gtk_widget_set_sensitive(fw,FALSE);
      
    gtk_widget_add_accelerator (std, "activate", panel->group, GDK_o, 0, 0);
    gtk_widget_add_accelerator (ply, "activate", panel->group, GDK_O, 
				GDK_SHIFT_MASK, 0);
      
    gtk_box_pack_start(GTK_BOX(box),ww,0,0,0);
    gtk_box_pack_start(GTK_BOX(box),box2,1,1,2);
    gtk_box_pack_start(GTK_BOX(box2),ply,1,1,0);
    

    gtk_table_attach_defaults(GTK_TABLE(mastertable),fw,0,1,6,7);
    gtk_table_attach_defaults(GTK_TABLE(mastertable),fr,1,2,6,7);

    gtk_table_attach_defaults(GTK_TABLE(mastertable),box,0,1,7,8);
    gtk_table_attach_defaults(GTK_TABLE(mastertable),std,1,2,7,8);

    {
      GtkWidget *active[2]={ply,std};
      outpanel_create(panel,ww,active);
    }
  }

  g_signal_connect (G_OBJECT (panel->toplevel), "delete_event",
		    G_CALLBACK (shutdown), NULL);
    
    
  gtk_widget_show_all(panel->toplevel);
  gtk_window_set_resizable(GTK_WINDOW(panel->toplevel),0);

}

static void feedback_process(postfish_mainpanel *panel){

  /* first order of business: release the play button if playback is
     no longer in progress */
  
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(panel->deckactive[3])))
    if(!playback_active)
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->deckactive[3]),0);

  /* second order of business; update the input meter if data is
     available and not dirtied by a seek */
  if(!playback_seeking){
    int     current_p=!output_feedback_deep();
    off_t   time_cursor;
    float *rms=alloca(sizeof(*rms)*(input_ch+OUTPUT_CHANNELS));
    float *peak=alloca(sizeof(*peak)*(input_ch+OUTPUT_CHANNELS));
    
    if(pull_output_feedback(peak,rms)){
      char buffer[14];
      int i;

      for(i=0;i<OUTPUT_CHANNELS;i++){
	if(peak[i]>=1.)
	  multibar_setwarn(MULTIBAR(panel->outbar),current_p);

	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(panel->channelshow[i]))){
	  peak[i]=todB(peak[i])*.5;
	  rms[i]=todB(rms[i])*.5;
	  
	  if(peak[i]>panel->outpeak){
	    panel->outpeak=ceil(peak[i]);
	    sprintf(buffer,"%+4.0fdB",panel->outpeak);
	    readout_set(READOUT(panel->outreadout),buffer);
	  }
	  
	}else{
	  peak[i]=-400;
	  rms[i]=-400;
	}
	
      }
      
      multibar_set(MULTIBAR(panel->outbar),rms,peak,OUTPUT_CHANNELS,current_p);
      
      if(pull_input_feedback(peak,rms,&time_cursor)){
	for(i=0;i<input_ch;i++){
	  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(panel->channelshow[i]))){
	    peak[i]=todB(peak[i])*.5;
	    rms[i]=todB(rms[i])*.5;
	    
	    if(peak[i]>panel->inpeak){
	      panel->inpeak=ceil(peak[i]);
	      sprintf(buffer,"%+4.0fdB",panel->inpeak);
	      readout_set(READOUT(panel->inreadout),buffer);
	    }

	  }else{
	    peak[i]=-400;
	    rms[i]=-400;
	  }
	}
	
	multibar_set(MULTIBAR(panel->inbar),rms,peak,input_ch,current_p);
	input_cursor_to_time(time_cursor,buffer);
	readout_set(READOUT(panel->cue),buffer);
      }
      
      clippanel_feedback(current_p);
      eqpanel_feedback(current_p);
      compandpanel_feedback(current_p);
      singlepanel_feedback(current_p);
      limitpanel_feedback(current_p);
      mixpanel_feedback(current_p);
      
    }
  }
}

static gboolean async_event_handle(GIOChannel *channel,
				   GIOCondition condition,
				   gpointer data){
  postfish_mainpanel *panel=data;
  char buf[1];
  read(eventpipe[0],buf,1);

  feedback_process(panel);
  return TRUE;
}

static int look_for_gtkrc(char *filename){
  FILE *f=fopen(filename,"r");
  if(!f)return 0;
  fprintf(stderr,"Loading postfish-gtkrc file found at %s\n",filename);
  gtk_rc_add_default_file(filename);
  return 1;
}

#include <stdlib.h>
void mainpanel_go(int argc,char *argv[], int ch){
  char *homedir=getenv("HOME");
  char *labels[33];
  char  buffer[20];
  int i;
  int found=0;
  memset(&p,0,sizeof(p));

  found|=look_for_gtkrc(ETCDIR"/postfish-gtkrc");
  {
    char *rcdir=getenv("HOME");
    if(rcdir){
      char *rcfile="/.postfish/postfish-gtkrc";
      char *homerc=calloc(1,strlen(rcdir)+strlen(rcfile)+1);
      strcat(homerc,homedir);
      strcat(homerc,rcfile);
      found|=look_for_gtkrc(homerc);
    }
  }
  {
    char *rcdir=getenv("POSTFISH_RCDIR");
    if(rcdir){
      char *rcfile="/postfish-gtkrc";
      char *homerc=calloc(1,strlen(rcdir)+strlen(rcfile)+1);
      strcat(homerc,homedir);
      strcat(homerc,rcfile);
      found|=look_for_gtkrc(homerc);
    }
  }
  found|=look_for_gtkrc("./postfish-gtkrc");

  if(!found){
  
    fprintf(stderr,"Postfish could not find the postfish-gtkrc configuration file normally\n"
	    "installed with Postfish and located in one of the following places:\n"

	    "\t./postfish-gtkrc\n"
	    "\t$(POSTFISHDIR)/postfish-gtkrc\n"
	    "\t~/.postfish/postfish-gtkrc\n\t"
	    ETCDIR"/postfish-gtkrc\n"
	    "This configuration file is used to tune the color, font and other detail aspects\n"
	    "of the Postfish user interface.  Although Postfish will work without it, the UI\n"
	    "appearence will likely make the application harder to use due to missing visual\n"
	    "cues.\n");
  }

  gtk_rc_add_default_file(ETCDIR"/postfish-gtkrc");
  if(homedir){
    char *rcfile="/.postfish-gtkrc";
    char *homerc=calloc(1,strlen(homedir)+strlen(rcfile)+1);
    strcat(homerc,homedir);
    strcat(homerc,rcfile);
    gtk_rc_add_default_file(homerc);
  }
  gtk_rc_add_default_file(".postfish-gtkrc");
  gtk_rc_add_default_file("postfish-gtkrc");
  gtk_init (&argc, &argv);

  memset(labels,0,sizeof(labels));
  if(ch==1){
    labels[0]="_1 mono";

    for(i=1;i<OUTPUT_CHANNELS;i++){
      sprintf(buffer,"_%d",i+1);
      labels[i]=strdup(buffer);
    }
  }else if (ch==2){
    labels[0]="_1 left";
    labels[1]="_2 right";
    for(i=2;i<OUTPUT_CHANNELS;i++){
      sprintf(buffer,"_%d",i+1);
      labels[i]=strdup(buffer);
    }
  }else if (ch>0 && ch<=MAX_INPUT_CHANNELS){
    for(i=0;i<ch && i<9;i++){
      sprintf(buffer,"_%d",i+1);
      labels[i]=strdup(buffer);
    }
    for(;i<ch;i++){
      sprintf(buffer,"%d_%d",(i+1)/10,(i+1)%10);
      labels[i]=strdup(buffer);
    }
    for(;i<OUTPUT_CHANNELS;i++){
      sprintf(buffer,"_%d",i+1);
      labels[i]=strdup(buffer);
    }

  }else{
    fprintf(stderr,"\nPostfish currently supports inputs of 1 to %d\n"
	    "channels (current input request: %d channels)\n\n",(int)MAX_INPUT_CHANNELS,ch);
    exit(1);
  }
  
  mainpanel_create(&p,labels);
  mainpanel_state_from_config(0);
  animate_fish(&p);

  /* set up watching the event pipe */
  {
    GIOChannel *channel = g_io_channel_unix_new (eventpipe[0]);
    guint id;

    g_io_channel_set_encoding (channel, NULL, NULL);
    g_io_channel_set_buffered (channel, FALSE);
    g_io_channel_set_close_on_unref (channel, TRUE);

    id = g_io_add_watch (channel, G_IO_IN, async_event_handle, &p);

    g_io_channel_unref (channel);

  }

  /* the slight race here is cosmetic, so I'm not worrying about it */
  main_looping=1;
  gtk_main ();

}

