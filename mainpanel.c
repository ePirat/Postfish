
#include <gtk/gtk.h>
#include "fisharray.h"
#include "multibar.h"
#define VERSION "$Id: mainpanel.c,v 1.1 2003/09/16 08:55:12 xiphmont Exp $"

typedef struct {
  GtkWidget *toplevel;
  GtkWidget *topframe;
  GtkWidget *toplabel;

  GtkWidget *mainbox;
  GtkWidget *leftbox;
  GtkWidget *panelframe;
  GtkWidget *rightbox;

  GtkWidget *wintable;
  GtkWidget *twirlimage;
  GdkPixmap *ff[12];
  GdkBitmap *fb[12];

  GtkWidget *buttonpanel[7];
  GtkWidget *buttonactive[7];

  GtkWidget *quitbutton;
} postfish_mainpanel;

static void shutdown(void){

  gtk_main_quit ();

}

static void mainpanel_panelentry(postfish_mainpanel *p,
				 char *label,
				 char *shortcut,
				 int i){
  p->buttonpanel[i]=gtk_check_button_new_with_mnemonic(label);
  p->buttonactive[i]=gtk_toggle_button_new_with_label(shortcut);
  
  gtk_table_attach_defaults(GTK_TABLE(p->wintable),p->buttonpanel[i],0,1,i+1,i+2);
  gtk_table_attach_defaults(GTK_TABLE(p->wintable),p->buttonactive[i],1,2,i+1,i+2);
}

void mainpanel_create(postfish_mainpanel *panel){
  int i;

  GdkWindow *root=gdk_get_default_root_window();
  panel->toplevel=gtk_window_new (GTK_WINDOW_TOPLEVEL);
  panel->topframe=gtk_frame_new (NULL);
  panel->toplabel=gtk_label_new (NULL);

  for(i=0;i<12;i++)
    panel->ff[i]=gdk_pixmap_create_from_xpm_d(root,
					      panel->fb+i,NULL,ff_xpm[i]);
  
  panel->mainbox=gtk_hbox_new(0,0);
  panel->leftbox=gtk_vbox_new(0,0);
  panel->rightbox=gtk_vbox_new(0,0);
  panel->wintable=gtk_table_new(8,2,0);
  panel->twirlimage=gtk_image_new_from_pixmap(panel->ff[0],panel->fb[0]);

  gtk_container_set_border_width (GTK_CONTAINER (panel->topframe), 3);
  gtk_container_set_border_width (GTK_CONTAINER (panel->mainbox), 3);
  gtk_frame_set_shadow_type(GTK_FRAME(panel->topframe),GTK_SHADOW_ETCHED_IN);
  gtk_frame_set_label_widget(GTK_FRAME(panel->topframe),panel->toplabel);
  gtk_label_set_markup(GTK_LABEL(panel->toplabel),
		       "<span size=\"large\" weight=\"bold\" "
		       "style=\"italic\" foreground=\"dark blue\">"
		       "Postfish</span> "VERSION);
  gtk_container_add (GTK_CONTAINER (panel->toplevel), panel->topframe);

  gtk_container_add (GTK_CONTAINER(panel->topframe), panel->mainbox);
  gtk_box_pack_start(GTK_BOX(panel->mainbox),panel->leftbox,0,0,0);
  gtk_box_pack_end(GTK_BOX(panel->mainbox),panel->rightbox,1,1,0);

  /* left side of main panel */
  {
    GtkWidget *tempf=gtk_frame_new(NULL);
    GtkWidget *temp=gtk_table_new(1,1,0);
    GtkWidget *tempa=gtk_alignment_new(0,1,0,0);
    gtk_frame_set_shadow_type(GTK_FRAME(tempf),GTK_SHADOW_ETCHED_IN);
    gtk_container_set_border_width (GTK_CONTAINER (tempf), 3);
    
    gtk_box_pack_end(GTK_BOX(panel->leftbox),temp,0,0,0);

    gtk_container_add (GTK_CONTAINER(tempf), panel->twirlimage);


    panel->quitbutton=gtk_button_new_with_mnemonic("quit");
    gtk_widget_set_name(panel->quitbutton,"quitbutton");
    gtk_container_add(GTK_CONTAINER(tempa),panel->quitbutton);
    
    gtk_table_attach_defaults(GTK_TABLE(temp),tempa,0,1,0,1);

    gtk_table_attach_defaults(GTK_TABLE(temp),tempf,0,1,0,1);

  }
  panel->panelframe=gtk_frame_new(NULL);
  gtk_container_set_border_width (GTK_CONTAINER (panel->panelframe), 3);
  gtk_container_set_border_width (GTK_CONTAINER (panel->wintable), 3);
  gtk_box_pack_start(GTK_BOX(panel->leftbox),panel->panelframe,0,0,0);
  gtk_frame_set_shadow_type(GTK_FRAME(panel->panelframe),GTK_SHADOW_ETCHED_IN);
  gtk_container_add(GTK_CONTAINER(panel->panelframe),panel->wintable);

  gtk_table_set_row_spacings(GTK_TABLE(panel->wintable),1);

  {
    GtkWidget *temp=gtk_label_new("visible");
    gtk_misc_set_alignment(GTK_MISC(temp),0,.5);
    gtk_table_attach_defaults(GTK_TABLE(panel->wintable),temp,0,1,0,1);

    temp=gtk_label_new("active");
    gtk_misc_set_alignment(GTK_MISC(temp),1,.5);
    gtk_table_attach_defaults(GTK_TABLE(panel->wintable),temp,1,2,0,1);
  }

  mainpanel_panelentry(panel,"_Declip ","d",0);
  mainpanel_panelentry(panel,"Cross_Talk ","t",1);
  mainpanel_panelentry(panel,"_Noise Filter ","n",2);
  mainpanel_panelentry(panel,"_Equalizer ","e",3);
  mainpanel_panelentry(panel,"_Compander ","c",4);
  mainpanel_panelentry(panel,"_Limiter ","l",5);
  mainpanel_panelentry(panel,"Cue_Sheet ","s",6);


  g_signal_connect (G_OBJECT (panel->toplevel), "delete_event",
		    G_CALLBACK (shutdown), NULL);
    
  g_signal_connect (G_OBJECT (panel->toplevel), "delete_event",
		    G_CALLBACK (shutdown), NULL);
    
  g_signal_connect (G_OBJECT (panel->quitbutton), "clicked",
		    G_CALLBACK (shutdown), NULL);

  /* right side of main panel */
  {
    char *labels[12]={"-96","-72","-48","-24","-20","-16","-12","-8","-4","0","+3","+6"};
    float levels[13]={-140,-96,-72,-48,-24,-20,-16,-12,-8,-4,0,+3,+6};
    GtkWidget *temp=multibar_new(12,labels,levels);
    gtk_box_pack_start(GTK_BOX(panel->rightbox),temp,0,0,0);
  }

  gtk_widget_show_all(panel->toplevel);
}

#include <stdlib.h>
int main(int argc, char *argv[]){
  postfish_mainpanel mainpanel;

  char *homedir=getenv("HOME");

  gtk_rc_add_default_file("/etc/postfish/postfishrc");
  if(homedir){
    char *rcfile="/.postfishrc";
    char *homerc=calloc(1,strlen(homedir)+strlen(rcfile)+1);
    strcat(homerc,homedir);
    strcat(homerc,rcfile);
    gtk_rc_add_default_file(homerc);
  }
  gtk_init (&argc, &argv);
  mainpanel_create(&mainpanel);


  gtk_main ();
}
