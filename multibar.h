#ifndef __MULTIBAR_H__
#define __MULTIBAR_H__

#include <sys/time.h>
#include <time.h>
#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkcontainer.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkdrawingarea.h>

G_BEGIN_DECLS

#define MULTIBAR_TYPE            (multibar_get_type ())
#define MULTIBAR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MULTIBAR_TYPE, Multibar))
#define MULTIBAR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MULTIBAR_TYPE, MultibarClass))
#define IS_MULTIBAR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MULTIBAR_TYPE))
#define IS_MULTIBAR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), MULTIBAR_TYPE))

typedef struct _Multibar       Multibar;
typedef struct _MultibarClass  MultibarClass;

typedef struct bartack {
  double pixelposhi;
  double pixelposlo;
  double pixeldeltahi;
  double pixeldeltalo;
} bartrack;

#define HI_ATTACK (1<<0)
#define LO_ATTACK (1<<1)
#define HI_DECAY  (1<<2)
#define LO_DECAY  (1<<3)

struct _Multibar{

  GtkDrawingArea canvas;  
  GdkPixmap *backing;

  int labels;
  PangoLayout **layout;
  double       *levels;

  GdkGC         *boxcolor;
  double         peak;

  struct timeval cliptimer;
  struct timeval peaktimer;

  bartrack *bartrackers;
  int bars;
  int dampen_flags;
};

struct _MultibarClass{

  GtkDrawingAreaClass parent_class;
  void (* multibar) (Multibar *m);

};

GType          multibar_get_type        (void);
GtkWidget*     multibar_new             (int n, char **labels, double *levels,
					 int flags);
void	       multibar_clear           (Multibar *m);
void	       multibar_set             (Multibar *m,double *lo,double *hi, int n);

G_END_DECLS

#endif

