#ifndef __MULTIBAR_H__
#define __MULTIBAR_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkcontainer.h>

G_BEGIN_DECLS

#define MULTIBAR_TYPE            (multibar_get_type ())
#define MULTIBAR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MULTIBAR_TYPE, Multibar))
#define MULTIBAR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MULTIBAR_TYPE, MultibarClass))
#define IS_MULTIBAR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MULTIBAR_TYPE))
#define IS_MULTIBAR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), MULTIBAR_TYPE))

typedef struct _Multibar       Multibar;
typedef struct _MultibarClass  MultibarClass;

struct _Multibar{
  GtkTable table;
  
  GtkWidget *buttons[3][3];
};

struct _MultibarClass
{
  GtkTableClass parent_class;

  void (* multibar) (Multibar *ttt);
};

GType          multibar_get_type        (void);
GtkWidget*     multibar_new             (void);
void	       multibar_clear           (Multibar *ttt);




#include <gtk/gtk.h>
#include <glib.h>

GType multibar_get_type (void){
  static GType multibar_type = 0;
  if (!multibar_type){
    static const GTypeInfo multibar_info = {
      sizeof (MultibarClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) multibar_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (Multibar),
      0,    /* n_preallocs */
      (GInstanceInitFunc) multibar_init,
    };

      ttt_type = g_type_register_static (GTK_TYPE_TABLE,
                                         "Multibar",
                                         &ttt_info,
                                         0);
    }

  return ttt_type;
}



typedef struct BarWidget {
  GtkWidget *toptable;
  GtkWidget **leftlabels;
  GtkWidget *canvasframe;
  GtkWidget *canvas
  GdkPixmap *backing;
} BarWidget;

/* Create a new backing pixmap of the appropriate size */
static gboolean configure_event(GtkWidget *widget, 
				GdkEventConfigure *event){
  if (pixmap)
    gdk_pixmap_unref(pixmap);

  pixmap = gdk_pixmap_new(widget->window,
			  widget->allocation.width,
			  widget->allocation.height,
			  -1);
  gdk_draw_rectangle (pixmap,
		      widget->style->white_gc,
		      TRUE,
		      0, 0,
		      widget->allocation.width,
		      widget->allocation.height);

  return TRUE;
}

static gboolean
expose_event( GtkWidget *widget, GdkEventExpose *event )
{
  gdk_draw_pixmap(widget->window,
		  widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
		  pixmap,
		  event->area.x, event->area.y,
		  event->area.x, event->area.y,
		  event->area.width, event->area.height);

  return FALSE;
}

