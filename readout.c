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

#include <stdlib.h>
#include "readout.h"

static GtkDrawingAreaClass *parent_class = NULL;

static void draw(GtkWidget *widget){
  GtkAllocation allocation;
  Readout *r=READOUT(widget);

  gtk_widget_get_allocation(widget, &allocation);
  int width=allocation.width;
  int height=allocation.height;
  int px,py;
  GdkGC *gc=gtk_widget_get_style(widget)->bg_gc[0];
  GdkGC *text_gc=gtk_widget_get_style(widget)->text_gc[0];
  GdkGC *light_gc=gtk_widget_get_style(widget)->light_gc[0];
  GdkGC *dark_gc=gtk_widget_get_style(widget)->dark_gc[0];

  /* blank pane */
  
  gdk_draw_rectangle(r->backing,gc,1,
		     0,0,width,height);

  /* draw layout */
  pango_layout_get_pixel_size(r->layout,&px,&py);

  gdk_draw_layout (r->backing,text_gc,
		   width-2-px-(py/4),(height-py)/2,
		   r->layout);
  
  /* draw frame */
  gdk_draw_rectangle(r->backing,dark_gc,0,
		     0,0,width-2,height-2);

  gdk_draw_line(r->backing,light_gc,1,1,width-3,1);
  gdk_draw_line(r->backing,light_gc,1,1,1,height-3);

  gdk_draw_line(r->backing,light_gc,width-1,0,width-1,height-1);
  gdk_draw_line(r->backing,light_gc,0,height-1,width-1,height-1);

}

static void draw_and_expose(GtkWidget *widget){
  GtkAllocation allocation;
  Readout *r=READOUT(widget);
  if(!GDK_IS_DRAWABLE(r->backing))return;
  draw(widget);
  if(!gtk_widget_is_drawable(widget))return;
  if(!GDK_IS_DRAWABLE(gtk_widget_get_window(widget)))return;
  gtk_widget_get_allocation(widget, &allocation);
  gdk_draw_drawable(gtk_widget_get_window(widget),
                    gtk_widget_get_style(widget)->fg_gc[gtk_widget_get_state (widget)],
                    r->backing,
                    0, 0,
                    0, 0,
                    allocation.width,             
                    allocation.height);
}

static gboolean expose( GtkWidget *widget, GdkEventExpose *event ){
  Readout *r=READOUT(widget);
  gdk_draw_drawable(gtk_widget_get_window(widget),
                    gtk_widget_get_style(widget)->fg_gc[gtk_widget_get_state (widget)],
                    r->backing,
                    event->area.x, event->area.y,
                    event->area.x, event->area.y,
                    event->area.width, event->area.height);
  
  return FALSE;
}

static void size_request (GtkWidget *widget,GtkRequisition *requisition){
  int x,y;
  Readout *r=READOUT(widget);
  
  pango_layout_get_pixel_size(r->layout,&x,&y);
  
  requisition->width = x+4+y/2;
  requisition->height = y+6;

}

static gboolean configure(GtkWidget *widget, GdkEventConfigure *event){
  GtkAllocation allocation;
  Readout *r=READOUT(widget);
  
  if (r->backing)
    g_object_unref(r->backing);
  
  gtk_widget_get_allocation(widget, &allocation);
  r->backing = gdk_pixmap_new(gtk_widget_get_window(widget),
                              allocation.width,
                              allocation.height,
                              -1);
  draw_and_expose(widget);

  return TRUE;
}

static void readout_class_init (ReadoutClass *class){
  GtkWidgetClass *widget_class = (GtkWidgetClass*) class;
  parent_class = g_type_class_peek_parent (class);

  widget_class->expose_event = expose;
  widget_class->configure_event = configure;
  widget_class->size_request = size_request;
}

static void readout_init (Readout *r){

}

GType readout_get_type (void){
  static GType m_type = 0;
  if (!m_type){
    static const GTypeInfo m_info={
      sizeof (ReadoutClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) readout_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (Readout),
      0,
      (GInstanceInitFunc) readout_init,
      0
    };
    
    m_type = g_type_register_static (GTK_TYPE_DRAWING_AREA, "Readout", &m_info, 0);
  }

  return m_type;
}

GtkWidget* readout_new (char *markup){
  GtkWidget *ret= GTK_WIDGET (g_object_new (readout_get_type (), NULL));
  Readout *r=READOUT(ret);

  r->layout=gtk_widget_create_pango_layout(ret,markup);

  return ret;
}

void readout_set(Readout  *r,char *label){
  pango_layout_set_text(r->layout,label,-1);
  draw_and_expose(GTK_WIDGET(r));
}

const gchar *readout_get(Readout  *r){
  return  pango_layout_get_text(r->layout);
}


