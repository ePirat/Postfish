
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <signal.h>
#include <stdio.h>
#include <pthread.h>
#include "fisharray.h"
#include "buttonicons.h"
#include "multibar.h"
#include "readout.h"
#include "version.h"

typedef struct {
  GtkWidget *topframe;
  GtkWidget *toplabel;

  GtkWidget *mainbox;
  GtkWidget *box1;
  GtkWidget *leftback;
  GtkWidget *leftframe;
  GtkWidget *box2;

  GtkWidget *wintable;
  GtkWidget *twirlimage;
  GdkPixmap *ff[19];
  GdkBitmap *fb[19];

  GtkWidget *buttonpanel[7];
  GtkWidget *quitbutton;
  
  GtkWidget *playimage;
  GdkPixmap *pf[2];
  GdkBitmap *pb[2];

  /* we need these widgets */
  GtkWidget *toplevel;

  GtkWidget *masterdB_r;
  GtkWidget *masterdB_s;
  GtkWidget *masterdB_a;

  GtkWidget *buttonactive[7];
  GtkWidget *cue_set[2];
  GtkWidget *cue_reset[2];

  GtkWidget *deckactive[7];

  /* ui state */
  int fishframe;
  int fishframe_init;
  guint fishframe_timer;
 
} postfish_mainpanel;

extern sig_atomic_t playback_active;
extern sig_atomic_t playback_exit;
extern sig_atomic_t ch;
extern void *playback_thread(void *dummy);

static void action_play(GtkWidget *dummy,postfish_mainpanel *p){
  if(!playback_active){
    pthread_t playback_thread_id;
    playback_active=1;
    animate_fish(p);
    pthread_create(&playback_thread_id,NULL,&playback_thread,NULL);
  }else{
    playback_exit=1;
    sched_yield();
    while(1){
      if(playback_active){
	sched_yield();
      }else
	break;
    }
  }
}

extern sig_atomic_t loop_active;
static void action_b(GtkWidget *widget,postfish_mainpanel *p){
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
    loop_active=1;
  else
    loop_active=0;

  gtk_image_set_from_pixmap(GTK_IMAGE(p->playimage),
			    p->pf[loop_active],
			    p->pb[loop_active]);
}

/* gotta have the Fucking Fish */
int reanimate_fish(postfish_mainpanel *p){
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

int animate_fish(postfish_mainpanel *p){
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

static void shutdown(void){
  gtk_main_quit ();
}

sig_atomic_t master_att;
static void masterdB_change(GtkRange *r, gpointer in){
  postfish_mainpanel *p=in;
  char buf[80];
  gdouble val=gtk_range_get_value(r);
  sprintf(buf,"%.1f dB",val);
  readout_set(READOUT(p->masterdB_r),buf);
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

      timeentry_fix(buffer);

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
      gtk_widget_child_focus(toplevel,GTK_DIR_TAB_BACKWARD);
      return TRUE;
    }

    pos--;
    if(pos==4 || pos==7 || pos==10)pos--;
    gtk_editable_set_position(GTK_EDITABLE(widget),pos);
    return  TRUE;

  case GDK_Right:
    if(pos>=12){
      /* advance focus */
      gtk_widget_child_focus(toplevel,GTK_DIR_TAB_FORWARD);
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

static gboolean keybinding(GtkWidget *widget,
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
  if(event->state&GDK_CONTROL_MASK) return FALSE;

  switch(event->keyval){
  case GDK_m:
    /* trigger master dB */
    gtk_widget_activate(p->masterdB_a);
    break;
  case GDK_minus:
    gtk_range_set_value(GTK_RANGE(p->masterdB_s),
			gtk_range_get_value(GTK_RANGE(p->masterdB_s))-.1);
    break;
  case GDK_underscore:
    gtk_range_set_value(GTK_RANGE(p->masterdB_s),
			gtk_range_get_value(GTK_RANGE(p->masterdB_s))-1.);
    break;
  case GDK_equal:
    gtk_range_set_value(GTK_RANGE(p->masterdB_s),
			gtk_range_get_value(GTK_RANGE(p->masterdB_s))+.1);
    break;
  case GDK_plus:
    gtk_range_set_value(GTK_RANGE(p->masterdB_s),
			gtk_range_get_value(GTK_RANGE(p->masterdB_s))+1.);
    break;
  case GDK_d:
    gtk_widget_activate(p->buttonactive[0]);
    break;
  case GDK_t:
    gtk_widget_activate(p->buttonactive[1]);
    break;
  case GDK_n:
    gtk_widget_activate(p->buttonactive[2]);
    break;
  case GDK_e:
    gtk_widget_activate(p->buttonactive[3]);
    break;
  case GDK_c:
    gtk_widget_activate(p->buttonactive[4]);
    break;
  case GDK_l:
    gtk_widget_activate(p->buttonactive[5]);
    break;
  case GDK_o:
    gtk_widget_activate(p->buttonactive[6]);
    break;
  case GDK_a:
    gtk_widget_activate(p->cue_set[0]);
    break;
  case GDK_A:
    gtk_widget_activate(p->cue_reset[0]);
    break;
  case GDK_b:
    gtk_widget_activate(p->cue_set[1]);
    break;
  case GDK_B:
    gtk_widget_activate(p->cue_reset[1]);
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

  return TRUE;
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

  GtkWidget *topplace,*topal;

  char *text_bar[7]={"[bksp]","[<]","[,]","[space]","[.]","[>]","[end]"};

  GdkWindow *root=gdk_get_default_root_window();
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
		    G_CALLBACK (keybinding), panel);
  

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
  panel->wintable=gtk_table_new(7,3,0);
  panel->twirlimage=gtk_image_new_from_pixmap(panel->ff[0],panel->fb[0]);

  gtk_container_set_border_width (GTK_CONTAINER (panel->topframe), 3);
  gtk_container_set_border_width (GTK_CONTAINER (panel->mainbox), 3);
  gtk_container_set_border_width (GTK_CONTAINER (panel->box1), 3);
  gtk_frame_set_shadow_type(GTK_FRAME(panel->topframe),GTK_SHADOW_ETCHED_IN);
  gtk_frame_set_label_widget(GTK_FRAME(panel->topframe),panel->toplabel);
  gtk_label_set_markup(GTK_LABEL(panel->toplabel),
		       "<span size=\"large\" weight=\"bold\" "
		       "style=\"italic\" foreground=\"dark blue\">"
		       "Postfish</span> "VERSION);

  gtk_container_add (GTK_CONTAINER(panel->topframe), panel->mainbox);
  gtk_box_pack_end(GTK_BOX(panel->mainbox),panel->box1,0,0,0);
  gtk_box_pack_start(GTK_BOX(panel->box1),panel->leftback,0,0,0);
  gtk_container_add (GTK_CONTAINER(panel->leftback), panel->leftframe);
  gtk_box_pack_start(GTK_BOX(panel->mainbox),panel->box2,0,0,0);
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

  mainpanel_panelentry(panel,"_Declip ","[d]",0);
  mainpanel_panelentry(panel,"Cross_Talk ","[t]",1);
  mainpanel_panelentry(panel,"_Noise Filter ","[n]",2);
  mainpanel_panelentry(panel,"_Equalizer ","[e]",3);
  mainpanel_panelentry(panel,"_Compander ","[c]",4);
  mainpanel_panelentry(panel,"_Limiter ","[l]",5);
  mainpanel_panelentry(panel,"_Output Cal. ","[o]",6);


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
    GtkWidget *togglebox=gtk_hbox_new(0,0);
    GtkWidget *in=gtk_label_new("in:");
    GtkWidget *out=gtk_label_new("out:");
    GtkWidget *show=gtk_label_new("show:");
    GtkWidget *inframe=gtk_frame_new(NULL);
    GtkWidget *outframe=gtk_frame_new(NULL);
    GtkWidget *inbar=multibar_new(12,labels,levels);
    GtkWidget *outbar=multibar_new(12,labels,levels);

    gtk_container_set_border_width(GTK_CONTAINER (ttable), 3);
    gtk_table_set_col_spacings(GTK_TABLE(ttable),5);
    gtk_misc_set_alignment(GTK_MISC(show),1,.5);
    gtk_misc_set_alignment(GTK_MISC(in),1,.5);
    gtk_misc_set_alignment(GTK_MISC(out),1,.5);
    gtk_box_set_spacing(GTK_BOX(togglebox),5);

    for(i=0;;i++){
      if(!chlabels[i])break;

      GtkWidget *button=gtk_check_button_new_with_mnemonic(chlabels[i]);
      if(i<2)gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button),TRUE);
      gtk_box_pack_start(GTK_BOX(togglebox),button,0,0,0);
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
    
    gtk_table_attach_defaults(GTK_TABLE(ttable),togglebox,1,3,0,1);
    gtk_table_attach(GTK_TABLE(ttable),show,0,1,0,1,GTK_FILL|GTK_SHRINK,GTK_FILL|GTK_SHRINK,0,0);
    gtk_table_attach(GTK_TABLE(ttable),in,0,1,1,2,GTK_FILL|GTK_SHRINK,GTK_FILL|GTK_SHRINK,0,0);
    gtk_table_attach(GTK_TABLE(ttable),out,0,1,2,3,GTK_FILL|GTK_SHRINK,GTK_FILL|GTK_SHRINK,0,0);
    gtk_table_attach_defaults(GTK_TABLE(ttable),inframe,1,3,1,2);
    gtk_table_attach_defaults(GTK_TABLE(ttable),outframe,1,3,2,3);


    /* master dB slider */
    {
      GtkWidget *box=gtk_hbox_new(0,0);

      panel->masterdB_a=gtk_toggle_button_new_with_label("[m]aster");
      panel->masterdB_r=readout_new("  0.0 dB");
      panel->masterdB_s=gtk_hscale_new_with_range(-50,50,.1);

      gtk_range_set_value(GTK_RANGE(panel->masterdB_s),0);
      gtk_scale_set_draw_value(GTK_SCALE(panel->masterdB_s),FALSE);
    
      gtk_table_attach(GTK_TABLE(ttable),panel->masterdB_a,0,1,3,4,
		       GTK_FILL,GTK_FILL,0,0);
      
      gtk_box_pack_start(GTK_BOX(box),panel->masterdB_r,0,0,0);
      gtk_box_pack_start(GTK_BOX(box),panel->masterdB_s,1,1,0);
      
      gtk_table_attach_defaults(GTK_TABLE(ttable),box,1,3,3,4);

      g_signal_connect_after (G_OBJECT(panel->masterdB_s), "value-changed",
			G_CALLBACK(masterdB_change), (gpointer)panel);
    }

    /* master action bar */
    {
      GtkWidget *bar_table=gtk_table_new(1,8,1);
      char buffer[20];
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


      g_signal_connect (G_OBJECT (panel->deckactive[3]), "clicked",
			G_CALLBACK (action_play), panel);


    }

    /* cue bar */
    {
      GtkWidget *cuebox=gtk_hbox_new(0,0);
      GtkWidget *cuelabel=gtk_label_new("cue:");
      GtkWidget *cue=readout_new("    :  :00.00");
      GtkWidget *entry_a=gtk_entry_new();
      GtkWidget *entry_b=gtk_entry_new();

      GtkWidget *framea=gtk_vseparator_new();
      GtkWidget *frameb=gtk_vseparator_new();

      GtkWidget *panelb=gtk_check_button_new_with_mnemonic("c_ue list");

      panel->cue_set[0]=gtk_button_new_with_label("[a]");
      panel->cue_set[1]=gtk_toggle_button_new_with_label("[b]");
      panel->cue_reset[0]=gtk_button_new_with_label("[A]");
      panel->cue_reset[1]=gtk_button_new_with_label("[B]");


      gtk_entry_set_width_chars(GTK_ENTRY(entry_a),13);
      gtk_entry_set_width_chars(GTK_ENTRY(entry_b),13);
      gtk_entry_set_text(GTK_ENTRY(entry_a),"    :  :00.00");
      gtk_entry_set_text(GTK_ENTRY(entry_b),"    :  :00.00");


      g_signal_connect (G_OBJECT (entry_a), "key-press-event",
			G_CALLBACK (timeevent_keybinding), panel->toplevel);
      g_signal_connect (G_OBJECT (entry_b), "key-press-event",
			G_CALLBACK (timeevent_keybinding), panel->toplevel);
      g_signal_connect_after(G_OBJECT (entry_a), "grab_focus",
			G_CALLBACK (timeevent_unselect), NULL);
      g_signal_connect_after(G_OBJECT (entry_b), "grab_focus",
			G_CALLBACK (timeevent_unselect), NULL);

      g_signal_connect (G_OBJECT (panel->cue_set[1]), "clicked",
			G_CALLBACK (action_b), panel);


      gtk_widget_set_name(panel->cue_reset[0],"reseta");
      gtk_widget_set_name(panel->cue_reset[1],"resetb");

      gtk_misc_set_alignment(GTK_MISC(cuelabel),1,.5);

      gtk_table_attach_defaults(GTK_TABLE(ttable),cuelabel,0,1,5,6);
      gtk_table_attach_defaults(GTK_TABLE(ttable),cuebox,1,2,5,6);
      gtk_table_attach_defaults(GTK_TABLE(ttable),panelb,2,3,5,6);

      gtk_box_pack_start(GTK_BOX(cuebox),cue,0,0,0);

      gtk_box_pack_start(GTK_BOX(cuebox),framea,1,1,3);

      gtk_box_pack_start(GTK_BOX(cuebox),panel->cue_set[0],0,0,0);
      gtk_box_pack_start(GTK_BOX(cuebox),entry_a,0,0,0);
      gtk_box_pack_start(GTK_BOX(cuebox),panel->cue_reset[0],0,0,0);

      gtk_box_pack_start(GTK_BOX(cuebox),frameb,1,1,3);

      gtk_box_pack_start(GTK_BOX(cuebox),panel->cue_set[1],0,0,0);
      gtk_box_pack_start(GTK_BOX(cuebox),entry_b,0,0,0);
      gtk_box_pack_start(GTK_BOX(cuebox),panel->cue_reset[1],0,0,0);

    }

    /* config bar */
    {
      GtkWidget *confbox=gtk_hbox_new(0,0);
      GtkWidget *conflabel=gtk_label_new("setting:");
      GtkWidget *conf=readout_new("");
      GtkWidget *panel=gtk_check_button_new_with_mnemonic("_setting list");
      gtk_misc_set_alignment(GTK_MISC(conflabel),1,.5);

      gtk_table_attach_defaults(GTK_TABLE(ttable),conflabel,0,1,6,7);
      gtk_table_attach(GTK_TABLE(ttable),confbox,1,2,6,7,
		       GTK_EXPAND|GTK_FILL,GTK_EXPAND|GTK_FILL,0,3);
      gtk_table_attach(GTK_TABLE(ttable),panel,2,3,6,7,0,0,0,0);

      gtk_box_pack_start(GTK_BOX(confbox),conf,1,1,0);
    }


    gtk_box_pack_end(GTK_BOX(panel->box2),ttable,0,0,0);

  }

  gtk_widget_show_all(panel->toplevel);
  gtk_window_set_resizable(GTK_WINDOW(panel->toplevel),0);

}

#include <stdlib.h>
void mainpanel_go(int argc,char *argv[]){
  postfish_mainpanel p;
  char *homedir=getenv("HOME");
  char *labels[9];
  char *labels_gen[]={"_0","_1","_2","_3","_4","_5","_6","_7",0};
  char *labels_st[]={"_0 left","_1 right","_2 mid","_3 side",0};
  memset(&p,0,sizeof(p));
  gtk_rc_add_default_file("/etc/postfish/postfishrc");
  if(homedir){
    char *rcfile="/.postfishrc";
    char *homerc=calloc(1,strlen(homedir)+strlen(rcfile)+1);
    strcat(homerc,homedir);
    strcat(homerc,rcfile);
    gtk_rc_add_default_file(homerc);
  }
  gtk_init (&argc, &argv);

  if(ch==2){
    memcpy(labels,labels_st,sizeof(labels_st));
  }else{
    memcpy(labels,labels_gen,sizeof(labels_gen));
    labels[ch]=0;
  }

  mainpanel_create(&p,labels);
  animate_fish(&p);
  gtk_main ();

}
















