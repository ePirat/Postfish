#include "postfish.h"

struct postfish_mainpanel;
typedef struct postfish_mainpanel postfish_mainpanel;

#include "clippanel.h"

struct postfish_mainpanel{
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

  GtkWidget *inbar;
  GtkWidget *outbar;

  GtkWidget *channelshow[10]; /* support only up to 8 + mid/side */

  GtkWidget *cue;
  GtkWidget *entry_a;
  GtkWidget *entry_b;


  /* ui state */
  int fishframe;
  int fishframe_init;
  guint fishframe_timer;
 
};

extern gboolean slider_keymodify(GtkWidget *w,GdkEventKey *event,gpointer in);
