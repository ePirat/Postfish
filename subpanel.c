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
#include "mainpanel.h"
#include "windowbutton.h"
#include "subpanel.h"

static int subpanel_hide(GtkWidget *widget,
			  GdkEvent *event,
			  gpointer in){
  subpanel_generic *p=in;
  if(p->mappedvar)*p->mappedvar=0;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->subpanel_windowbutton),0);
  return TRUE;
}

static int windowbutton_action(GtkWidget *widget,
			gpointer in){
  int active;
  subpanel_generic *p=in;
  if(widget==p->subpanel_windowbutton){
    active=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->subpanel_windowbutton));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->mainpanel_windowbutton),active);
  }else{
    active=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->mainpanel_windowbutton));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->subpanel_windowbutton),active);
  }
  
  if(active){
    if(p->mappedvar)*p->mappedvar=1;
    gtk_widget_show(p->subpanel_toplevel);
  }else{
    if(p->mappedvar)*p->mappedvar=0;
    gtk_widget_hide(p->subpanel_toplevel);
  }

  return FALSE;
}

static int activebutton_action(GtkWidget *widget,
			gpointer in){
  subpanel_generic *p=in;
  int active,i;

  for(i=0;i<p->active_button_count;i++){
    if(widget==p->subpanel_activebutton[i]){
      active=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->subpanel_activebutton[i]));
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->mainpanel_activebutton[i]),active);
      p->activevar[i]=active;
      if(p->callback)p->callback(p->callback_pointer,i);
      break;
    }
    if(widget==p->mainpanel_activebutton[i]){
      active=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->mainpanel_activebutton[i]));
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->subpanel_activebutton[i]),active);
      p->activevar[i]=active;
      break;
    }
  }
  
  return FALSE;
}

static gboolean rebind_space(GtkWidget *widget,
			     GdkEventKey *event,
			     gpointer in){
  /* do not capture Alt accellerators */
  if(event->state&GDK_MOD1_MASK) return FALSE;
  if(event->state&GDK_CONTROL_MASK) return FALSE;

  if(event->keyval==GDK_space){
    subpanel_generic *p=in;
    GdkEvent copy=*(GdkEvent *)event;
    copy.any.window=p->mainpanel->toplevel->window;
    gtk_main_do_event((GdkEvent *)(&copy));
    return TRUE;
  }
  return FALSE;
}

static gboolean forward_events(GtkWidget *widget,
			       GdkEvent *event,
			       gpointer in){

  subpanel_generic *p=in;
  GdkEventKey *kevent=(GdkEventKey *)event;
  
  /* if this is a shortcutless panel, check first for a numeral
     keypress; the intent of this mechanism is to handle focus
     rotation on the activation buttons on a panel handling > 10
     channels with multiple activation buttons. */
    
  int keych=kevent->keyval-GDK_0,active;
  if(keych==0)
    keych=9;
  else 
    keych--;
  
  if(keych>=0 && keych<=9){
    if(input_ch>9){
      int actualch=keych+p->rotation[keych]*10;

      if(actualch<p->active_button_start)return TRUE;
      if(actualch>=p->active_button_start+p->active_button_count)return TRUE;

      gtk_widget_grab_focus(p->subpanel_activebutton[actualch-p->active_button_start]);

      p->rotation[keych]++;
      if(keych+p->rotation[keych]*10+p->active_button_start>=input_ch)p->rotation[keych]=0;

    }else{
      if(keych<p->active_button_start)return TRUE;
      if(keych>=p->active_button_start+p->active_button_count)return TRUE;

      active=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->subpanel_activebutton[keych-p->active_button_start]));
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->subpanel_activebutton[keych-p->active_button_start]),!active);

    }
  }else{
    GdkEvent copy=*(GdkEvent *)event;
    copy.any.window=p->mainpanel->toplevel->window;
    gtk_main_do_event((GdkEvent *)(&copy));
  }
  return TRUE;
}

void subpanel_show_all_but_toplevel(subpanel_generic *s){
  gtk_widget_show_all(s->subpanel_topframe);
}

subpanel_generic *subpanel_create(postfish_mainpanel *mp,
				  GtkWidget *windowbutton,
				  GtkWidget **activebutton,
				  sig_atomic_t *activevar,
				  sig_atomic_t *mappedvar,
				  char *prompt,char **shortcut,
				  int start,int num){

  subpanel_generic *panel=calloc(1,sizeof(*panel));

  GtkWidget *toplabelbox=gtk_event_box_new();
  GtkWidget *toplabelframe=gtk_frame_new(NULL);
  GtkWidget *toplabel=gtk_hbox_new(0,0);
  GtkWidget *toplabelwb=windowbutton_new(prompt);
  GtkWidget *toplabelab[num];
  int i;

  for(i=0;i<num;i++){
    if(shortcut && shortcut[i]){
      toplabelab[i]=gtk_toggle_button_new_with_label(shortcut[i]);
    }else{
      char buf[80];
      sprintf(buf," %d ",i+start+1);
	toplabelab[i]=gtk_toggle_button_new_with_label(buf);
    }
  }

  panel->active_button_count=num;
  panel->active_button_start=start;

  panel->subpanel_topframe=gtk_frame_new(NULL);
  panel->subpanel_windowbutton=toplabelwb;
  panel->subpanel_activebutton=malloc(num*sizeof(*panel->subpanel_activebutton));
  memcpy(panel->subpanel_activebutton,toplabelab,num*sizeof(*toplabelab));

  panel->mainpanel_windowbutton=windowbutton;
  panel->mainpanel_activebutton=calloc(num,sizeof(*activebutton));
  memcpy(panel->mainpanel_activebutton,activebutton,num*sizeof(*activebutton));
  panel->activevar=activevar;
  panel->mappedvar=mappedvar;
  
  gtk_box_pack_start(GTK_BOX(toplabel),toplabelwb,0,0,5);
  for(i=num-1;i>=0;i--)
    gtk_box_pack_end(GTK_BOX(toplabel),toplabelab[i],0,0,1);

  gtk_widget_set_name(toplabelwb,"panelbutton");
  gtk_widget_set_name(toplabelbox,"panelbox");
  gtk_frame_set_shadow_type(GTK_FRAME(toplabelframe),GTK_SHADOW_ETCHED_IN);
  gtk_container_add(GTK_CONTAINER (toplabelbox), toplabelframe);
  gtk_container_add(GTK_CONTAINER (toplabelframe), toplabel);

  panel->subpanel_box=gtk_vbox_new (0,0);

  panel->subpanel_toplevel=gtk_window_new (GTK_WINDOW_TOPLEVEL);
  panel->mainpanel=mp;

  panel->group = gtk_accel_group_new ();
  gtk_window_add_accel_group (GTK_WINDOW(panel->subpanel_toplevel), panel->group);

  gtk_container_add (GTK_CONTAINER (panel->subpanel_toplevel), panel->subpanel_topframe);
  gtk_container_add (GTK_CONTAINER (panel->subpanel_topframe), panel->subpanel_box);
  gtk_container_set_border_width (GTK_CONTAINER (panel->subpanel_topframe), 3);
  gtk_container_set_border_width (GTK_CONTAINER (panel->subpanel_box), 5);
  gtk_frame_set_shadow_type(GTK_FRAME(panel->subpanel_topframe),GTK_SHADOW_NONE);
  gtk_frame_set_label_widget(GTK_FRAME(panel->subpanel_topframe),toplabelbox);

    
  /* space *always* means play/pause */
  g_signal_connect (G_OBJECT (panel->subpanel_toplevel), "key-press-event",
		    G_CALLBACK (rebind_space), 
		    panel);
  /* forward unhandled events to the main window */
  g_signal_connect_after (G_OBJECT (panel->subpanel_toplevel), "key-press-event",
			  G_CALLBACK (forward_events), 
			  panel);

  /* delete should == hide */
  g_signal_connect (G_OBJECT (panel->subpanel_toplevel), "delete-event",
		    G_CALLBACK (subpanel_hide), 
		    panel);

  gtk_widget_add_accelerator(windowbutton, "clicked", panel->group, GDK_W, GDK_MOD1_MASK, 0);


  /* link the mainpanel and subpanel buttons */
  g_signal_connect_after (G_OBJECT (panel->mainpanel_windowbutton), "clicked",
			  G_CALLBACK (windowbutton_action), panel);
  g_signal_connect_after (G_OBJECT (panel->subpanel_windowbutton), "clicked",
			  G_CALLBACK (windowbutton_action), panel);

  for(i=0;i<num;i++){
    g_signal_connect_after (G_OBJECT (panel->mainpanel_activebutton[i]), "clicked",
			    G_CALLBACK (activebutton_action), panel);
    g_signal_connect_after (G_OBJECT (panel->subpanel_activebutton[i]), "clicked",
			    G_CALLBACK (activebutton_action), panel);
  }

  gtk_window_set_resizable(GTK_WINDOW(panel->subpanel_toplevel),0);

  return panel;
}

void subpanel_set_active_callback(subpanel_generic *s,
				  gpointer in,
				  void (*callback)(gpointer in,int)){
  s->callback_pointer=in;
  s->callback=callback;
}
