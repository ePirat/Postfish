/*
 *
 *  postfish
 *    
 *      Copyright (C) 2002-2005 Monty
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

#ifndef __READOUT_H__
#define __READOUT_H__

#include <sys/time.h>
#include <time.h>
#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkcontainer.h>
#include <gtk/gtkdrawingarea.h>
#include <gdk/gdkdrawable.h>

G_BEGIN_DECLS

#define READOUT_TYPE            (readout_get_type ())
#define READOUT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), READOUT_TYPE, Readout))
#define READOUT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), READOUT_TYPE, ReadoutClass))
#define IS_READOUT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), READOUT_TYPE))
#define IS_READOUT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), READOUT_TYPE))

typedef struct _Readout       Readout;
typedef struct _ReadoutClass  ReadoutClass;

struct _Readout{

  GtkDrawingArea canvas;  
  GdkPixmap *backing;
  PangoLayout *layout;
};

struct _ReadoutClass{

  GtkDrawingAreaClass parent_class;
  void (* readout) (Readout *m);

};

GType          readout_get_type        (void);
GtkWidget*     readout_new             (char *markup);
void	       readout_set             (Readout *m,char *markup);
const gchar*   readout_get             (Readout  *r);

G_END_DECLS

#endif

