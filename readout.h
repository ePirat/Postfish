#ifndef __READOUT_H__
#define __READOUT_H__

#include <sys/time.h>
#include <time.h>
#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkcontainer.h>
#include <gtk/gtkframe.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkeventbox.h>

G_BEGIN_DECLS

#define READOUT_TYPE            (readout_get_type ())
#define READOUT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), READOUT_TYPE, Readout))
#define READOUT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), READOUT_TYPE, ReadoutClass))
#define IS_READOUT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), READOUT_TYPE))
#define IS_READOUT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), READOUT_TYPE))

typedef struct _Readout       Readout;
typedef struct _ReadoutClass  ReadoutClass;

struct _Readout{

  GtkFrame frame;
  GtkWidget *label;
  GtkWidget *sizelabel;
};

struct _ReadoutClass{

  GtkFrameClass parent_class;
  void (* readout) (Readout *m);

};

GType          readout_get_type        (void);
GtkWidget*     readout_new             (char *markup);
void	       readout_set             (Readout *m,char *markup);
const gchar*   readout_get             (Readout  *r);

G_END_DECLS

#endif

