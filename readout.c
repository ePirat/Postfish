#include "readout.h"

static GtkFrameClass *parent_class = NULL;

static void readout_class_init (ReadoutClass *class){
  parent_class = g_type_class_peek_parent (class);
}

static void readout_init (Readout *r){
  GtkWidget *widget=GTK_WIDGET(r);

  r->box=gtk_event_box_new();
  r->label=gtk_label_new(NULL);
  gtk_container_add(GTK_CONTAINER(widget),r->box);
  gtk_container_add(GTK_CONTAINER(r->box),r->label);
  gtk_misc_set_alignment(GTK_MISC(r->label),1.0,.5);
  gtk_misc_set_padding(GTK_MISC(r->label),4,0);
  gtk_widget_show(r->box);
  gtk_widget_show(r->label);
}

GType readout_get_type (void){
  static GType m_type = 0;
  if (!m_type){
    static const GTypeInfo m_info={
      sizeof (ReadoutClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) readout_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (Readout),
      0,
      (GInstanceInitFunc) readout_init,
      0
    };
    
    m_type = g_type_register_static (GTK_TYPE_FRAME, "Readout", &m_info, 0);
  }

  return m_type;
}

GtkWidget* readout_new (char *markup){
  GtkWidget *ret= GTK_WIDGET (g_object_new (readout_get_type (), NULL));
  Readout *r=READOUT(ret);

  readout_set(r,markup);
  return ret;
}

void readout_set(Readout  *r,char *label){
  gtk_label_set_markup(GTK_LABEL(r->label),label);
}


