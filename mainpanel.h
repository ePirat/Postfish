/*
 *
 *  postfish
 *    
 *      Copyright (C) 2002-2004 Monty
 *
 *  Postfish is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  Postfish is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with Postfish; see the file COPYING.  If not, write to the
 *  Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * 
 */

#include "postfish.h"

struct postfish_mainpanel;
typedef struct postfish_mainpanel postfish_mainpanel;

#include "clippanel.h"
#include "eqpanel.h"
#include "compandpanel.h"
#include "singlepanel.h"
#include "limitpanel.h"

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

  GtkWidget *cue_act[2];
  GtkWidget *cue_set[2];
  GtkWidget *cue_reset[2];

  GtkWidget *deckactive[7];

  GtkWidget *inbar;
  GtkWidget *outbar;
  GtkWidget *inreadout;
  GtkWidget *outreadout;

  float inpeak;
  float outpeak;

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
