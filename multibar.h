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

#define HI_ATTACK   (1<<0)
#define LO_ATTACK   (1<<1)
#define HI_DECAY    (1<<2)
#define LO_DECAY    (1<<3)
#define ZERO_DAMP   (1<<4)
#define PEAK_FOLLOW (1<<5)


struct _Multibar{

  GtkDrawingArea canvas;  
  GdkPixmap *backing;

  int labels;
  int readout;
  PangoLayout **layout;
  double       *levels;

  GdkGC         *boxcolor;
  double         peak;
  int            peakdelay;
  double         peakdelta;

  int            clipdelay;

  bartrack *bartrackers;
  int bars;
  int dampen_flags;

  int    thumbs;
  double thumbval[3];
  int    thumbpixel[3];
  GtkStateType thumbstate[3];
  int    thumbfocus;
  int    prev_thumbfocus;
  int    thumbgrab;
  int    thumbx;
  double thumblo;
  double thumbhi;
  double thumbsmall;
  double thumblarge;

  int    thumblo_x;
  int    thumbhi_x;

  int       xpad;

  void  (*callback)(GtkWidget *,gpointer);
  gpointer callbackp;
};

struct _MultibarClass{

  GtkDrawingAreaClass parent_class;
  void (* multibar) (Multibar *m);
};

GType          multibar_get_type        (void);
GtkWidget*     multibar_new             (int n, char **labels, double *levels,
					 int thumbs, int flags);
GtkWidget*     multibar_slider_new (int n, char **labels, double *levels, 
				    int thumbs);

void	       multibar_clear           (Multibar *m);
void	       multibar_set             (Multibar *m,double *lo,double *hi, int n);
void	       multibar_thumb_set       (Multibar *m,double v, int n);
void	       multibar_setwarn         (Multibar *m);
void           multibar_reset           (Multibar *m);
void           multibar_callback        (Multibar *m,
					 void (*callback)
					 (GtkWidget *,gpointer),
					 gpointer);
double multibar_get_value(Multibar *m,int n);
void multibar_thumb_bounds(Multibar *m,double lo, double hi);
void multibar_thumb_increment(Multibar *m,double small, double large);

G_END_DECLS

#endif


