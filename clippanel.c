
#include "postfish.h"
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "readout.h"
#include "mainpanel.h"
#include "clippanel.h"

void clippanel_show(postfish_clippanel *p){
  gtk_widget_show_all(p->toplevel);
}
void clippanel_hide(postfish_clippanel *p){
  gtk_widget_hide_all(p->toplevel);
}

extern gboolean mainpanel_keybinding(GtkWidget *widget,
				     GdkEventKey *event,
				     gpointer in);

void clippanel_create(postfish_clippanel *panel,postfish_mainpanel *mp){
  GdkWindow *root=gdk_get_default_root_window();
  GtkWidget *topframe=gtk_frame_new (NULL);
  GtkWidget *toplabel=gtk_label_new (" declipping filter ");
  GtkWidget *topplace=gtk_table_new(1,1,0);
  GtkWidget *topal=gtk_alignment_new(1,0,0,0);
  GtkWidget *closebutton=gtk_button_new_with_mnemonic("_X");
  gtk_widget_set_name(closebutton,"quitbutton");

  panel->toplevel=gtk_window_new (GTK_WINDOW_TOPLEVEL);
  panel->mainpanel=mp;
  gtk_container_add (GTK_CONTAINER(topal), closebutton);
  
  gtk_table_attach_defaults(GTK_TABLE(topplace),
			    topal,0,1,0,1);
  gtk_table_attach_defaults(GTK_TABLE(topplace),
			    topframe,0,1,0,1);
    
  gtk_container_add (GTK_CONTAINER (panel->toplevel), topplace);
  gtk_container_set_border_width (GTK_CONTAINER (closebutton), 3);

  gtk_container_set_border_width (GTK_CONTAINER (topframe), 3);
  gtk_frame_set_shadow_type(GTK_FRAME(topframe),GTK_SHADOW_ETCHED_IN);
  gtk_frame_set_label_widget(GTK_FRAME(topframe),toplabel);



#if 0
  g_signal_connect (G_OBJECT (panel->quitbutton), "clicked",
		    G_CALLBACK (shutdown), NULL);
#endif
  

  /* no, no; forward events, don't call the keyhandler directly; that
     way accellerators go too */
  g_signal_connect_after (G_OBJECT (panel->toplevel), "key-press-event",
			  G_CALLBACK (mainpanel_keybinding), 
			  panel->mainpanel);


  gtk_window_set_resizable(GTK_WINDOW(panel->toplevel),0);
  
}
