
#include <gtk/gtk.h>
#include "fisharray.h"
#include "buttonicons.h"
#include "multibar.h"
#include "readout.h"
#define VERSION "$Id: mainpanel.c,v 1.2 2003/10/08 21:22:38 xiphmont Exp $ "

typedef struct {
  GtkWidget *toplevel;
  GtkWidget *topframe;
  GtkWidget *toplabel;

  GtkWidget *mainbox;
  GtkWidget *leftbox;
  GtkWidget *leftback;
  GtkWidget *leftframe;
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

void mainpanel_create(postfish_mainpanel *panel,char **chlabels){
  int i;
  GdkPixmap *xpm_bar[7];
  GdkBitmap *xbm_bar[7];
  GtkWidget *gim_bar[7];

  char *text_bar[7]={"[bksp]","<",",","[space]",".",">","[end]"};

  GdkWindow *root=gdk_get_default_root_window();
  panel->toplevel=gtk_window_new (GTK_WINDOW_TOPLEVEL);
  panel->topframe=gtk_frame_new (NULL);
  panel->toplabel=gtk_label_new (NULL);

  for(i=0;i<12;i++)
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
  for(i=0;i<7;i++)
    gim_bar[i]=gtk_image_new_from_pixmap(xpm_bar[i],xbm_bar[i]);

  panel->mainbox=gtk_hbox_new(0,0);
  panel->leftback=gtk_event_box_new();
  panel->leftbox=gtk_event_box_new();
  panel->leftframe=gtk_frame_new(NULL);
  panel->rightbox=gtk_vbox_new(0,0);
  panel->leftbox=gtk_vbox_new(0,0);
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
  gtk_box_pack_start(GTK_BOX(panel->leftbox),panel->leftback,0,0,0);
  gtk_container_add (GTK_CONTAINER(panel->leftback), panel->leftframe);
  gtk_box_pack_end(GTK_BOX(panel->mainbox),panel->rightbox,0,0,0);
  gtk_widget_set_name(panel->leftback,"winpanel");

  /* left side of main panel */

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

  mainpanel_panelentry(panel,"_Declip ","d",0);
  mainpanel_panelentry(panel,"Cross_Talk ","t",1);
  mainpanel_panelentry(panel,"_Noise Filter ","n",2);
  mainpanel_panelentry(panel,"_Equalizer ","e",3);
  mainpanel_panelentry(panel,"_Compander ","c",4);
  mainpanel_panelentry(panel,"_Limiter ","l",5);
  mainpanel_panelentry(panel,"_Output Cal. ","o",6);


  g_signal_connect (G_OBJECT (panel->toplevel), "delete_event",
		    G_CALLBACK (shutdown), NULL);
    
  g_signal_connect (G_OBJECT (panel->toplevel), "delete_event",
		    G_CALLBACK (shutdown), NULL);
    
  /* right side of main panel */
  {
    char *labels[12]={"-96","-72","-48","-24","-20","-16",
		      "-12","-8","-4","0","+3","+6"};
    float levels[13]={-140,-96,-72,-48,-24,-20,-16,
		      -12,-8,-4,0,+3,+6};

    GtkWidget *ttable=gtk_table_new(7,2,0);
    GtkWidget *togglebox2=gtk_hbox_new(0,0);
    GtkWidget *togglebox=gtk_hbox_new(0,0);
    GtkWidget *toggleal=gtk_alignment_new(0,1,0,0);
    GtkWidget *in=gtk_label_new("in");
    GtkWidget *out=gtk_label_new("out");
    GtkWidget *inframe=gtk_frame_new(NULL);
    GtkWidget *outframe=gtk_frame_new(NULL);
    GtkWidget *inbar=multibar_new(12,labels,levels);
    GtkWidget *outbar=multibar_new(12,labels,levels);

    gtk_container_set_border_width(GTK_CONTAINER (ttable), 3);
    gtk_table_set_col_spacings(GTK_TABLE(ttable),5);
    gtk_misc_set_alignment(GTK_MISC(in),1,.5);
    gtk_misc_set_alignment(GTK_MISC(out),1,.5);
    gtk_box_set_spacing(GTK_BOX(togglebox),5);
    gtk_box_set_spacing(GTK_BOX(togglebox2),5);
    
    gtk_box_pack_start(GTK_BOX(togglebox),toggleal,0,0,0);
    gtk_container_add(GTK_CONTAINER(toggleal),togglebox2);

    for(i=0;;i++){
      if(!chlabels[i])break;

      GtkWidget *button=gtk_check_button_new_with_mnemonic(chlabels[i]);
      if(i<2)gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button),TRUE);
      gtk_box_pack_start(GTK_BOX(togglebox2),button,0,0,0);
      {
	char buffer[]="color\0\0";
	buffer[5]= (i%10)+48;
	gtk_widget_set_name(button,buffer);
      }
    }

    gtk_frame_set_shadow_type(GTK_FRAME(inframe),GTK_SHADOW_ETCHED_IN);
    gtk_frame_set_shadow_type(GTK_FRAME(outframe),GTK_SHADOW_ETCHED_IN);
    gtk_container_add(GTK_CONTAINER(inframe),inbar);
    gtk_container_add(GTK_CONTAINER(outframe),outbar);
    
    gtk_table_attach_defaults(GTK_TABLE(ttable),togglebox,1,2,0,1);
    gtk_table_attach(GTK_TABLE(ttable),in,0,1,1,2,GTK_FILL|GTK_SHRINK,GTK_FILL|GTK_SHRINK,0,0);
    gtk_table_attach(GTK_TABLE(ttable),out,0,1,2,3,GTK_FILL|GTK_SHRINK,GTK_FILL|GTK_SHRINK,0,0);
    gtk_table_attach_defaults(GTK_TABLE(ttable),inframe,1,2,1,2);
    gtk_table_attach_defaults(GTK_TABLE(ttable),outframe,1,2,2,3);


    /* twirlfish */
    {
      GtkWidget *tempf=gtk_frame_new(NULL);
      gtk_frame_set_shadow_type(GTK_FRAME(tempf),GTK_SHADOW_NONE);
      gtk_container_set_border_width (GTK_CONTAINER (tempf), 0);
      gtk_box_pack_end(GTK_BOX(togglebox),tempf,0,0,0);
      gtk_container_add (GTK_CONTAINER(tempf), panel->twirlimage);
      
    }

    /* master dB slider */
    {
      GtkWidget *box=gtk_hbox_new(0,0);
      GtkWidget *active=gtk_toggle_button_new_with_mnemonic("_master");
      GtkWidget *label=readout_new(" -50.0 dB");
      GtkWidget *slider=gtk_hscale_new_with_range(-50,50,.1);

      gtk_scale_set_draw_value(GTK_SCALE(slider),FALSE);
    
      gtk_table_attach(GTK_TABLE(ttable),active,0,1,3,4,
		       GTK_FILL,GTK_FILL,0,0);
      
      gtk_box_pack_start(GTK_BOX(box),label,0,0,0);
      gtk_box_pack_start(GTK_BOX(box),slider,1,1,0);
      
      gtk_table_attach_defaults(GTK_TABLE(ttable),box,1,2,3,4);
    }

    /* master action bar */
    {
      GtkWidget *bar_table=gtk_table_new(1,8,1);
      GtkWidget *buttons[7];
      char buffer[20];
      for(i=0;i<7;i++){
	GtkWidget *box=gtk_vbox_new(0,3);
	GtkWidget *label=gtk_label_new(text_bar[i]);

	if(i==3)
	  buttons[i]=gtk_toggle_button_new();
	else
	  buttons[i]=gtk_button_new();

	gtk_box_pack_start(GTK_BOX(box),gim_bar[i],0,0,0);
	gtk_box_pack_start(GTK_BOX(box),label,0,0,0);
	gtk_container_add (GTK_CONTAINER(buttons[i]), box);
      }

      gtk_table_attach_defaults(GTK_TABLE(bar_table),buttons[0],0,1,0,1);
      gtk_table_attach_defaults(GTK_TABLE(bar_table),buttons[1],1,2,0,1);
      gtk_table_attach_defaults(GTK_TABLE(bar_table),buttons[2],2,3,0,1);
      gtk_table_attach_defaults(GTK_TABLE(bar_table),buttons[3],3,5,0,1);
      gtk_table_attach_defaults(GTK_TABLE(bar_table),buttons[4],5,6,0,1);
      gtk_table_attach_defaults(GTK_TABLE(bar_table),buttons[5],6,7,0,1);
      gtk_table_attach_defaults(GTK_TABLE(bar_table),buttons[6],7,8,0,1);

      gtk_table_attach(GTK_TABLE(ttable),bar_table,1,2,4,5,
		       GTK_EXPAND|GTK_FILL,GTK_EXPAND|GTK_FILL,
		       0,5);
    }

    /* cue bar */
    {
      GtkWidget *cuebox=gtk_hbox_new(0,0);
      GtkWidget *cuelabel=gtk_label_new("cue");
      GtkWidget *cue=readout_new("9999:99:99.99");
      GtkWidget *entry_a=gtk_entry_new();
      GtkWidget *entry_b=gtk_entry_new();

      GtkWidget *framea=gtk_vseparator_new();
      GtkWidget *frameb=gtk_vseparator_new();

      GtkWidget *set_a=gtk_button_new_with_mnemonic("_a");
      GtkWidget *set_b=gtk_button_new_with_mnemonic("_b");
      GtkWidget *reset_a=gtk_button_new_with_mnemonic("_A");
      GtkWidget *reset_b=gtk_button_new_with_mnemonic("_B");

      GtkWidget *panel=gtk_toggle_button_new_with_mnemonic("Cue_sheet");

      gtk_entry_set_width_chars(GTK_ENTRY(entry_a),13);
      gtk_entry_set_width_chars(GTK_ENTRY(entry_b),13);
      gtk_entry_set_text(GTK_ENTRY(entry_a),"    :  :00.00");
      gtk_entry_set_text(GTK_ENTRY(entry_b),"    :  :00.00");


      gtk_widget_set_name(reset_a,"reseta");
      gtk_widget_set_name(reset_b,"resetb");

      gtk_misc_set_alignment(GTK_MISC(cuelabel),1,.5);


      gtk_table_attach_defaults(GTK_TABLE(ttable),cuelabel,0,1,5,6);
      gtk_table_attach_defaults(GTK_TABLE(ttable),cuebox,1,2,5,6);

      gtk_box_pack_start(GTK_BOX(cuebox),cue,0,0,0);

      gtk_box_pack_start(GTK_BOX(cuebox),framea,1,1,3);

      gtk_box_pack_start(GTK_BOX(cuebox),set_a,0,0,0);
      gtk_box_pack_start(GTK_BOX(cuebox),entry_a,0,0,0);
      gtk_box_pack_start(GTK_BOX(cuebox),reset_a,0,0,0);

      gtk_box_pack_start(GTK_BOX(cuebox),frameb,1,1,3);

      gtk_box_pack_start(GTK_BOX(cuebox),set_b,0,0,0);
      gtk_box_pack_start(GTK_BOX(cuebox),entry_b,0,0,0);
      gtk_box_pack_start(GTK_BOX(cuebox),reset_b,0,0,0);

      gtk_box_pack_end(GTK_BOX(cuebox),panel,0,0,3);

    }

      gtk_box_pack_start(GTK_BOX(panel->rightbox),ttable,0,0,0);

    /* quitbutton */
    {
      GtkWidget *tempa=gtk_alignment_new(1,1,0,0);
      panel->quitbutton=gtk_button_new_with_mnemonic("quit");
      gtk_widget_set_name(panel->quitbutton,"quitbutton");
      gtk_box_pack_end(GTK_BOX(panel->rightbox),tempa,0,0,0);
      gtk_container_add (GTK_CONTAINER(tempa), panel->quitbutton);
      g_signal_connect (G_OBJECT (panel->quitbutton), "clicked",
			G_CALLBACK (shutdown), NULL);
    }

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

  {
    char *labels[]={"left","right","mid","side",0};
    mainpanel_create(&mainpanel,labels);
  }

  gtk_main ();
}
