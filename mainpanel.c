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
#include "version.h"
#include "input.h"
#include "output.h"
#include "mainpanel.h"
#include "windowbutton.h"

static void meterhold_reset(postfish_mainpanel *p){
  p->inpeak=-200;
  p->outpeak=-200;
  
  readout_set(READOUT(p->inreadout),"------");
  readout_set(READOUT(p->outreadout),"------");
}

static void action_zero(GtkWidget *widget,postfish_mainpanel *p){
  const char *time=gtk_entry_get_text(GTK_ENTRY(p->entry_a));
  off_t cursor=input_time_to_cursor(time);

  output_halt_playback();
  input_seek(cursor);
  readout_set(READOUT(p->cue),(char *)time);
  multibar_reset(MULTIBAR(p->inbar));
  multibar_reset(MULTIBAR(p->outbar));
  clippanel_reset();
  eqpanel_reset();
  compandpanel_reset();
  singlepanel_reset();
  limitpanel_reset();
  meterhold_reset(p);
}

static void action_end(GtkWidget *widget,postfish_mainpanel *p){
  off_t cursor=input_time_to_cursor("9999:59:59.99");
  char buf[14];

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
	gtk_timeout_add(rand()%1000*30,(GtkFunction)reanimate_fish,p);
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
	gtk_timeout_add(10,(GtkFunction)reanimate_fish,p);
      return FALSE;
    }
    if(p->fishframe==0){
      /* reschedule to blink */
      p->fishframe_timer=
	gtk_timeout_add(rand()%1000*30,(GtkFunction)reanimate_fish,p);
      return FALSE;
    }
  }
  return TRUE;
}

static void animate_fish(postfish_mainpanel *p){
  if(p->fishframe_init){
    gtk_timeout_remove(p->fishframe_timer);
    p->fishframe_timer=
      gtk_timeout_add(80,(GtkFunction)reanimate_fish,p);
  }else{
    p->fishframe_init=1;
    p->fishframe_timer=
      gtk_timeout_add(rand()%1000*30,(GtkFunction)reanimate_fish,p);
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

static void action_seta(GtkWidget *widget,postfish_mainpanel *p){
  const char *time=gtk_entry_get_text(GTK_ENTRY(p->entry_a));
  
  time=readout_get(READOUT(p->cue));
  gtk_entry_set_text(GTK_ENTRY(p->entry_a),time);

}

static void action_setb(GtkWidget *widget,postfish_mainpanel *p){
  const char *time=gtk_entry_get_text(GTK_ENTRY(p->entry_b));
  off_t cursora,cursorb=input_time_to_cursor(time);
  time=gtk_entry_get_text(GTK_ENTRY(p->entry_a));
  cursora=input_time_to_cursor(time);
  
  time=readout_get(READOUT(p->cue));
  cursorb=input_time_to_cursor(time);
  gtk_entry_set_text(GTK_ENTRY(p->entry_b),time);
    
}

static void action_reseta(GtkWidget *widget,postfish_mainpanel *p){
  char time[14];
  input_cursor_to_time(0,time);
  gtk_entry_set_text(GTK_ENTRY(p->entry_a),time);
  input_Acursor_set(0);
}

static void action_resetb(GtkWidget *widget,postfish_mainpanel *p){
  char time[14];
  input_cursor_to_time(0,time);
  gtk_entry_set_text(GTK_ENTRY(p->entry_b),time);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->cue_act[1]),0);
}

static void shutdown(void){
  gtk_main_quit ();
}

sig_atomic_t master_att;
static void masterdB_change(GtkWidget *dummy, gpointer in){
  postfish_mainpanel *p=in;
  char buf[80];
  float val=multibar_get_value(MULTIBAR(p->masterdB_s),0);
  sprintf(buf,"%.1fdB",val);
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

  /* do not capture Alt accellerators */
  if(event->state&GDK_MOD1_MASK) return FALSE;

  if(event->state&GDK_CONTROL_MASK){

    switch(event->keyval){
    case GDK_a:
    case GDK_A:
      gtk_widget_activate(p->cue_reset[0]);
      break;
    case GDK_b:
    case GDK_B:
      gtk_widget_activate(p->cue_reset[1]);
      break;

    default:
    return FALSE;
  }


  }else{
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
    case GDK_t:
      /* trigger master dB */
      gtk_widget_activate(p->masterdB_a);
      break;
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
    case GDK_d:
      gtk_widget_activate(p->buttonactive[0]);
      break;
    case GDK_c:
      gtk_widget_activate(p->buttonactive[1]);
      break;
    case GDK_m:
      gtk_widget_activate(p->buttonactive[2]);
      break;
    case GDK_s:
      gtk_widget_activate(p->buttonactive[3]);
      break;
    case GDK_v:
      gtk_widget_activate(p->buttonactive[4]);
      break;
    case GDK_e:
      gtk_widget_activate(p->buttonactive[5]);
      break;
    case GDK_l:
      gtk_widget_activate(p->buttonactive[6]);
      break;
    case GDK_a:
      gtk_widget_activate(p->cue_act[0]);
      break;
    case GDK_A:
      gtk_widget_activate(p->cue_set[0]);
      break;
    case GDK_b:
      gtk_widget_activate(p->cue_act[1]);
      break;
    case GDK_B:
      gtk_widget_activate(p->cue_set[1]);
      break;
    case GDK_BackSpace:
      gtk_widget_activate(p->deckactive[0]);
      break;
    case GDK_less:
      gtk_widget_activate(p->deckactive[1]);
      break;
    case GDK_comma:
      gtk_widget_activate(p->deckactive[2]);
      break;
    case GDK_space:
      gtk_widget_activate(p->deckactive[3]);
      break;
    case GDK_period:
      gtk_widget_activate(p->deckactive[4]);
      break;
    case GDK_greater:
      gtk_widget_activate(p->deckactive[5]);
      break;
    case GDK_End:
      gtk_widget_activate(p->deckactive[6]);
      break;
    default:
      return FALSE;
    }
  }
  return TRUE;
}

static void mainpanel_panelentry(postfish_mainpanel *p,
				 char *label,
				 char *shortcut,
				 int i,
				 void (*panel_create)
				 (postfish_mainpanel *,
				  GtkWidget *, GtkWidget *)){  
  
  GtkWidget *ww=windowbutton_new(label);
  GtkWidget *wa=gtk_toggle_button_new_with_label(shortcut);
  
  gtk_table_attach_defaults(GTK_TABLE(p->wintable),ww,0,1,i+1,i+2);
  gtk_table_attach_defaults(GTK_TABLE(p->wintable),wa,1,2,i+1,i+2);

  p->buttonactive[i]=wa;
  if(panel_create)(*panel_create)(p,ww,wa);
}

void mainpanel_create(postfish_mainpanel *panel,char **chlabels){
  int i;
  GdkPixmap *xpm_bar[7];
  GdkBitmap *xbm_bar[7];
  GtkWidget *gim_bar[7];

  GtkWidget *topplace,*topal;

  char *text_bar[7]={"[bksp]","[<]","[,]","[space]","[.]","[>]","[end]"};
  GdkWindow *root=gdk_get_default_root_window();

  char versionmarkup[240];
  char *version=strstr(VERSION,"version.h,v");
  if(version){
    char *versionend=strchr(version,' ');
    if(versionend)versionend=strchr(versionend+1,' ');
    if(versionend)versionend=strchr(versionend+1,' ');
    if(versionend)versionend=strchr(versionend+1,' ');
    if(versionend){
      int len=versionend-version-11;
      version=strdup(version+12);
      version[len-1]=0;
    }
  }else{
    version="";
  }
  snprintf(versionmarkup,240," <span size=\"large\" weight=\"bold\" "
	   "style=\"italic\" foreground=\"dark blue\">"
	   "Postfish</span>  <span size=\"small\" foreground=\"#606060\">"
	   "version %s</span> ",
	   version);

  panel->toplevel=gtk_window_new (GTK_WINDOW_TOPLEVEL);
  panel->topframe=gtk_frame_new (NULL);
  panel->toplabel=gtk_label_new (NULL);

  topplace=gtk_table_new(1,1,0);
  topal=gtk_alignment_new(1,0,0,0);

  panel->quitbutton=gtk_button_new_with_mnemonic("_quit");
  gtk_widget_set_name(panel->quitbutton,"quitbutton");
  gtk_container_add (GTK_CONTAINER(topal), panel->quitbutton);
  
  gtk_table_attach_defaults(GTK_TABLE(topplace),
			    topal,0,1,0,1);
  gtk_table_attach_defaults(GTK_TABLE(topplace),
			    panel->topframe,0,1,0,1);
    
  gtk_container_add (GTK_CONTAINER (panel->toplevel), topplace);
  gtk_container_set_border_width (GTK_CONTAINER (panel->quitbutton), 3);

  g_signal_connect (G_OBJECT (panel->quitbutton), "clicked",
		    G_CALLBACK (shutdown), NULL);
  

  g_signal_connect (G_OBJECT (panel->toplevel), "key-press-event",
		    G_CALLBACK (mainpanel_keybinding), panel);
  

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

  panel->mainbox=gtk_hbox_new(0,6);
  panel->leftback=gtk_event_box_new();
  panel->box1=gtk_event_box_new();
  panel->leftframe=gtk_frame_new(NULL);
  panel->box2=gtk_vbox_new(0,0);
  panel->box1=gtk_vbox_new(0,6);
  panel->wintable=gtk_table_new(6,3,0);
  panel->twirlimage=gtk_image_new_from_pixmap(panel->ff[0],panel->fb[0]);

  gtk_container_set_border_width (GTK_CONTAINER (panel->topframe), 3);
  gtk_container_set_border_width (GTK_CONTAINER (panel->mainbox), 3);
  gtk_container_set_border_width (GTK_CONTAINER (panel->box1), 3);
  gtk_frame_set_shadow_type(GTK_FRAME(panel->topframe),GTK_SHADOW_ETCHED_IN);
  gtk_frame_set_label_widget(GTK_FRAME(panel->topframe),panel->toplabel);
  gtk_label_set_markup(GTK_LABEL(panel->toplabel),versionmarkup);

  gtk_container_add (GTK_CONTAINER(panel->topframe), panel->mainbox);
  gtk_box_pack_end(GTK_BOX(panel->mainbox),panel->box1,0,0,0);
  gtk_box_pack_start(GTK_BOX(panel->box1),panel->leftback,0,0,0);
  gtk_container_add (GTK_CONTAINER(panel->leftback), panel->leftframe);
  gtk_box_pack_start(GTK_BOX(panel->mainbox),panel->box2,0,0,0);
  gtk_widget_set_name(panel->leftback,"winpanel");

  /* left side of main panel */
  {
    char *labels[12]={"-96","-72","-60","-48","-36","-24",
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

    panel->inbar=multibar_new(12,labels,levels, 0,
			      LO_ATTACK|LO_DECAY|HI_DECAY|PEAK_FOLLOW );
    panel->outbar=multibar_new(12,labels,levels, 0,
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
      char *sliderlabels[10]={"-40","-30","-20","-10","0","+10","+20","+30","+40","+50"};
      float sliderlevels[11]={-50,-40,-30,-20,-10,0,10,20,30,40,50};
      
      GtkWidget *box=gtk_hbox_new(0,0);

      GtkWidget *masterlabel=gtk_label_new("master:");
      panel->masterdB_a=gtk_toggle_button_new_with_label("a[t]ten");
      panel->masterdB_r=readout_new("  0.0dB");
      panel->masterdB_s=multibar_slider_new(10,sliderlabels,sliderlevels,1);
      
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

      g_signal_connect_after (G_OBJECT(panel->masterdB_a), "clicked",
			G_CALLBACK(masterdB_change), (gpointer)panel);
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


    }

    /* cue bar */
    {
      GtkWidget *cuebox=gtk_hbox_new(0,0);
      GtkWidget *cuelabel=gtk_label_new("cue:");

      GtkWidget *framea=gtk_vseparator_new();
      GtkWidget *frameb=gtk_vseparator_new();

      GtkWidget *panelb=windowbutton_new("c_ue list");

      panel->entry_a=gtk_entry_new();
      panel->entry_b=gtk_entry_new();

      panel->cue=readout_new("    :  :00.00");


      panel->cue_act[0]=gtk_button_new_with_label("[a]");
      panel->cue_act[1]=gtk_toggle_button_new_with_label("[b]");
      panel->cue_set[0]=gtk_button_new_with_label("[A]");
      panel->cue_set[1]=gtk_button_new_with_label("[B]");
      panel->cue_reset[0]=gtk_button_new_with_label("^A");
      panel->cue_reset[1]=gtk_button_new_with_label("^B");


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

      g_signal_connect (G_OBJECT (panel->cue_act[0]), "clicked",
			G_CALLBACK (action_entrya), panel);
      g_signal_connect (G_OBJECT (panel->cue_act[1]), "clicked",
			G_CALLBACK (action_entryb), panel);

      g_signal_connect (G_OBJECT (panel->cue_set[0]), "clicked",
			G_CALLBACK (action_seta), panel);
      g_signal_connect (G_OBJECT (panel->cue_set[1]), "clicked",
			G_CALLBACK (action_setb), panel);

      g_signal_connect (G_OBJECT (panel->cue_reset[0]), "clicked",
			G_CALLBACK (action_reseta), panel);
      g_signal_connect (G_OBJECT (panel->cue_reset[1]), "clicked",
			G_CALLBACK (action_resetb), panel);


      gtk_widget_set_name(panel->cue_reset[0],"reseta");
      gtk_widget_set_name(panel->cue_reset[1],"resetb");

      gtk_misc_set_alignment(GTK_MISC(cuelabel),1,.5);

      gtk_table_attach_defaults(GTK_TABLE(ttable),cuelabel,0,1,5,6);
      gtk_table_attach_defaults(GTK_TABLE(ttable),cuebox,1,2,5,6);
      gtk_table_attach_defaults(GTK_TABLE(ttable),panelb,2,3,5,6);

      gtk_box_pack_start(GTK_BOX(cuebox),panel->cue,0,0,0);

      gtk_box_pack_start(GTK_BOX(cuebox),framea,1,1,3);

      gtk_box_pack_start(GTK_BOX(cuebox),panel->cue_act[0],0,0,0);
      gtk_box_pack_start(GTK_BOX(cuebox),panel->entry_a,0,0,0);
      gtk_box_pack_start(GTK_BOX(cuebox),panel->cue_set[0],0,0,0);
      gtk_box_pack_start(GTK_BOX(cuebox),panel->cue_reset[0],0,0,0);

      gtk_box_pack_start(GTK_BOX(cuebox),frameb,1,1,3);

      gtk_box_pack_start(GTK_BOX(cuebox),panel->cue_act[1],0,0,0);
      gtk_box_pack_start(GTK_BOX(cuebox),panel->entry_b,0,0,0);
      gtk_box_pack_start(GTK_BOX(cuebox),panel->cue_set[1],0,0,0);
      gtk_box_pack_start(GTK_BOX(cuebox),panel->cue_reset[1],0,0,0);

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
      gtk_table_attach(GTK_TABLE(ttable),panel,2,3,6,7,0,0,0,0);

      gtk_box_pack_start(GTK_BOX(confbox),conf,1,1,0);
    }


    gtk_box_pack_end(GTK_BOX(panel->box2),ttable,0,0,0);

  }

  /* right side of main panel */

  //gtk_container_set_border_width (GTK_CONTAINER (panel->leftframe), 3);
  gtk_container_set_border_width (GTK_CONTAINER (panel->wintable), 3);
  gtk_frame_set_shadow_type(GTK_FRAME(panel->leftframe),GTK_SHADOW_ETCHED_IN);
  gtk_container_add(GTK_CONTAINER(panel->leftframe),panel->wintable);

  gtk_table_set_row_spacings(GTK_TABLE(panel->wintable),1);

  {
    GtkWidget *temp=gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(temp),"<span size=\"x-small\">visible</span>");
    gtk_misc_set_alignment(GTK_MISC(temp),0,.5);
    gtk_table_attach_defaults(GTK_TABLE(panel->wintable),temp,0,1,0,1);

    temp=gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(temp),"<span size=\"x-small\">active</span>");
    gtk_misc_set_alignment(GTK_MISC(temp),1,.5);
    gtk_table_attach_defaults(GTK_TABLE(panel->wintable),temp,1,2,0,1);
  }

  mainpanel_panelentry(panel,"_Declipper ","[d]",0,clippanel_create);
  mainpanel_panelentry(panel,"_Crosstalk ","[c]",1,0);
  mainpanel_panelentry(panel,"_Multicomp ","[m]",2,compandpanel_create);
  mainpanel_panelentry(panel,"_Singlecomp ","[s]",3,singlepanel_create);
  mainpanel_panelentry(panel,"De_verber ","[v]",4,suppresspanel_create);
  mainpanel_panelentry(panel,"_Equalizer ","[e]",5,eqpanel_create);
  mainpanel_panelentry(panel,"_Limiter ","[l]",6,limitpanel_create);

  g_signal_connect (G_OBJECT (panel->toplevel), "delete_event",
		    G_CALLBACK (shutdown), NULL);
    
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
    int     n=(input_ch>1?input_ch+2:input_ch);
    float *rms=alloca(sizeof(*rms)*(input_ch+2));
    float *peak=alloca(sizeof(*peak)*(input_ch+2));
    
    if(pull_output_feedback(peak,rms)){
      char buffer[14];
      int i;

      for(i=0;i<n;i++){
	if(i<input_ch && peak[i]>=1.)
	  multibar_setwarn(MULTIBAR(panel->outbar),current_p);

	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(panel->channelshow[i]))){
	  peak[i]=todB(peak[i]);
	  rms[i]=todB(rms[i]);

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
      
      multibar_set(MULTIBAR(panel->outbar),rms,peak,n,current_p);
      
      if(pull_input_feedback(peak,rms,&time_cursor)){
	for(i=0;i<n;i++){
	  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(panel->channelshow[i]))){
	    peak[i]=todB(peak[i]);
	    rms[i]=todB(rms[i]);

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
	
	multibar_set(MULTIBAR(panel->inbar),rms,peak,n,current_p);
	input_cursor_to_time(time_cursor,buffer);
	readout_set(READOUT(panel->cue),buffer);
      }
      
      clippanel_feedback(current_p);
      eqpanel_feedback(current_p);
      compandpanel_feedback(current_p);
      singlepanel_feedback(current_p);
      limitpanel_feedback(current_p);
      
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

#include <stdlib.h>
void mainpanel_go(int argc,char *argv[], int ch){
  postfish_mainpanel p;
  char *homedir=getenv("HOME");
  char *labels[11];
  char  buffer[20];
  int i;

  memset(&p,0,sizeof(p));
  gtk_rc_add_default_file("/etc/postfish/postfish-gtkrc");
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
  switch(ch){
  case 1:
    labels[0]="_0 mono";
    break;
  case 2:
    labels[0]="_0 left";
    labels[1]="_1 right";
    labels[2]="_2 mid";
    labels[3]="_3 side";
    break;
  case 3:
  case 4:
  case 5:
  case 6:
  case 7:
  case 8:
    for(i=0;i<ch;i++){
      sprintf(buffer,"_%d",i);
      labels[i]=strdup(buffer);
    }
    sprintf(buffer,"_%d mid",i);
    labels[i++]=strdup(buffer);
    sprintf(buffer,"_%d div",i);
    labels[i++]=strdup(buffer);
    break;
  default:
    fprintf(stderr,"\nPostfish currently supports inputs of one to eight\n"
	    "channels (current input request: %d channels)\n\n",ch);
    exit(1);
  }

  mainpanel_create(&p,labels);
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


  gtk_main ();

}


