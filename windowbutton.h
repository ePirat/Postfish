#ifndef __WINDOWBUTTON_H__
#define __WINDOWBUTTON_H__

#include <sys/time.h>
#include <time.h>
#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkcheckbutton.h>

G_BEGIN_DECLS

#define WINDOWBUTTON_TYPE            (windowbutton_get_type ())
#define WINDOWBUTTON(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), WINDOWBUTTON_TYPE, Windowbutton))
#define WINDOWBUTTON_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), WINDOWBUTTON_TYPE, WindowbuttonClass))
#define IS_WINDOWBUTTON(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), WINDOWBUTTON_TYPE))
#define IS_WINDOWBUTTON_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), WINDOWBUTTON_TYPE))

typedef struct _Windowbutton       Windowbutton;
typedef struct _WindowbuttonClass  WindowbuttonClass;

struct _Windowbutton{

  GtkCheckButton frame;

};

struct _WindowbuttonClass{

  GtkCheckButtonClass parent_class;
  void (* windowbutton) (Windowbutton *m);

};

GType          windowbutton_get_type        (void);
GtkWidget*     windowbutton_new             (char *markup);

G_END_DECLS

#endif

