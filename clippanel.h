#include "postfish.h"

typedef struct postfish_clippanel {
  GtkWidget *toplevel;

  postfish_mainpanel *mainpanel;
} postfish_clippanel;


extern void clippanel_create(postfish_clippanel *panel,
			     postfish_mainpanel *mp);
extern void clippanel_show(postfish_clippanel *p);
extern void clippanel_hide(postfish_clippanel *p);
