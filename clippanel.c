
#include "postfish.h"
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "readout.h"
#include "mainpanel.h"

void clippanel_show(postfish_clippanel *p){
  gtk_widget_show_all(p->toplevel);
}
void clippanel_hide(postfish_clippanel *p){
  gtk_widget_hide_all(p->toplevel);
}

static gboolean forward_events(GtkWidget *widget,
			       GdkEvent *event,
			       gpointer in){
  postfish_clippanel *p=in;
  GdkEvent copy=*(GdkEvent *)event;
  copy.any.window=p->mainpanel->toplevel->window;
  gtk_main_do_event((GdkEvent *)(&copy));
  return TRUE;
}

void clippanel_create(postfish_clippanel *panel,postfish_mainpanel *mp){
  GdkWindow *root=gdk_get_default_root_window();
  GtkWidget *topframe=gtk_frame_new (NULL);
  GtkWidget *toplabel=gtk_label_new (NULL);
  GtkWidget *topvbox=gtk_vbox_new (0,0);

  GtkWidget *indframe=gtk_frame_new (NULL);
  GtkWidget *allframe=gtk_frame_new (NULL);
  GtkWidget *indcheck=
    gtk_radio_button_new_with_label (NULL, 
				     "individual channels ");
  GtkWidget *allcheck=
    gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON(indcheck), 
						 "all channels ");

  gtk_label_set_markup(GTK_LABEL(toplabel),
		       "<span weight=\"bold\" "
		       "style=\"italic\">"
		       " declipping filter </span>");

  gtk_frame_set_label_widget(GTK_FRAME(indframe),indcheck);
  gtk_frame_set_label_widget(GTK_FRAME(allframe),allcheck);

  gtk_box_pack_end(GTK_BOX(topvbox),allframe,0,1,4);
  gtk_box_pack_end(GTK_BOX(topvbox),indframe,0,1,4);

  panel->toplevel=gtk_window_new (GTK_WINDOW_TOPLEVEL);
  panel->mainpanel=mp;
    
  gtk_container_add (GTK_CONTAINER (panel->toplevel), topframe);
  gtk_container_add (GTK_CONTAINER (topframe), topvbox);
  gtk_container_set_border_width (GTK_CONTAINER (topframe), 3);
  gtk_container_set_border_width (GTK_CONTAINER (topvbox), 5);
  gtk_frame_set_shadow_type(GTK_FRAME(topframe),GTK_SHADOW_ETCHED_IN);
  gtk_frame_set_label_widget(GTK_FRAME(topframe),toplabel);

    



  /* forward unhandled events to the main window */
  g_signal_connect_after (G_OBJECT (panel->toplevel), "key-press-event",
			  G_CALLBACK (forward_events), 
			  panel);


  gtk_window_set_resizable(GTK_WINDOW(panel->toplevel),0);
  
}
