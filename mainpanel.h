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
#include "deverbpanel.h"
#include "limitpanel.h"
#include "mixpanel.h"
#include "reverbpanel.h"
#include "outpanel.h"

struct postfish_mainpanel{
  GtkWidget *twirlimage;
  GdkPixmap *ff[19];
  GdkBitmap *fb[19];

  GtkWidget *playimage;
  GdkPixmap *pf[2];
  GdkBitmap *pb[2];

  /* we need these widgets */
  GtkAccelGroup *group;
  GtkWidget *toplevel;

  GtkWidget *masterdB_r;
  GtkWidget *masterdB_s;
  GtkWidget *masterdB_a;

  GtkWidget *buttonactive[7];
  GtkWidget *deckactive[7];

  GtkWidget *inbar;
  GtkWidget *outbar;
  GtkWidget *inreadout;
  GtkWidget *outreadout;

  float inpeak;
  float outpeak;

  GtkWidget *channelshow[MAX_INPUT_CHANNELS]; 
  int channelrotate[10]; /* up to 16 channels, but only base 10 digits on the keyboard */
  GtkWidget ***channel_wa;
  int channeleffects;

  GtkWidget *cue;
  GtkWidget *cue_a;
  GtkWidget *entry_a;
  GtkWidget *set_a;
  GtkWidget *cue_b;
  GtkWidget *entry_b;
  GtkWidget *set_b;

  /* ui state */
  int fishframe;
  int fishframe_init;
  guint fishframe_timer;
  int *ch_rotate;

};

extern void mainpanel_go(int n,char *list[],int ch);
extern void save_state();
