#include "windowbutton.h"

static GtkCheckButtonClass *parent_class = NULL;

static void draw_triangle (GtkStyle      *style,
			   GdkWindow     *window,
			   GtkStateType   state_type,
			   GtkShadowType  shadow_type,
			   gint           x,
			   gint           y,
			   gint           size){

  GdkGC *gc=style->bg_gc[state_type];
  GdkGC *light_gc=style->light_gc[state_type];
  GdkGC *dark_gc=style->dark_gc[state_type];
  GdkGC *black_gc=style->black_gc;
  int i;
  
  /* fill the main triangle */
  for(i=0;i<size;i++)
    gdk_draw_line(window,gc,x+i,y+((i)>>1),x+i,y+size-1-((i)>>1));


  /* draw border */
  switch(shadow_type){
  case GTK_SHADOW_ETCHED_IN:
    for(i=0;i<size;i++){
      if(y+size-1-(i>>1) > y+(i>>1)+1){
	gdk_draw_point(window,dark_gc,x+i,y+size-1-(i>>1)-1);
	gdk_draw_point(window,light_gc,x+i,y+(i>>1)+1);
      }
      gdk_draw_point(window,light_gc,x+i,y+size-1-(i>>1));
      gdk_draw_point(window,dark_gc,x+i,y+(i>>1));
    } 
    gdk_draw_line(window,dark_gc,x,y,x,y+size-1);
    gdk_draw_line(window,light_gc,x+1,y+1,x+1,y+size-2);
    break;

  case GTK_SHADOW_IN:
    for(i=0;i<size;i++){
      if(y+size-1-(i>>1) > y+(i>>1)+1)
	gdk_draw_point(window,black_gc,x+i,y+(i>>1)+1);
      gdk_draw_point(window,light_gc,x+i,y+size-1-(i>>1));
      gdk_draw_point(window,dark_gc,x+i,y+(i>>1));    
    } 
    gdk_draw_line(window,dark_gc,x,y,x,y+size-1);
    gdk_draw_line(window,black_gc,x+1,y+1,x+1,y+size-2);
    break;


  case GTK_SHADOW_OUT:
    for(i=0;i<size;i++){
      if(y+size-1-(i>>1)-1 > y+(i>>1))
	gdk_draw_point(window,dark_gc,x+i,y+size-1-(i>>1)-1);
      gdk_draw_point(window,light_gc,x+i,y+(i>>1));    
      gdk_draw_point(window,black_gc,x+i,y+size-1-(i>>1));
    } 
    gdk_draw_line(window,light_gc,x,y,x,y+size-2);
    break;
  case GTK_SHADOW_NONE:
    break;
  case GTK_SHADOW_ETCHED_OUT:
    /* unimplemented, unused */
    break;
  }

}

static void windowbutton_draw_indicator (GtkCheckButton *check_button,
					 GdkRectangle   *area){
  GtkWidget *widget;
  GtkWidget *child;
  GtkButton *button;
  GtkToggleButton *toggle_button;
  GtkStateType state_type;
  GtkShadowType shadow_type;
  gint x, y;
  gint indicator_size;
  gint indicator_spacing;
  gint focus_width;
  gint focus_pad;
  gboolean interior_focus;
  
  if (GTK_WIDGET_DRAWABLE (check_button)){
    
    widget = GTK_WIDGET (check_button);
    button = GTK_BUTTON (check_button);
    toggle_button = GTK_TOGGLE_BUTTON (check_button);
    
    gtk_widget_style_get (widget, "interior_focus", &interior_focus,
			  "focus-line-width", &focus_width, 
			  "focus-padding", &focus_pad, NULL);
    
    _gtk_check_button_get_props (check_button, &indicator_size, 
				 &indicator_spacing);
    
    x = widget->allocation.x + 
      indicator_spacing + GTK_CONTAINER (widget)->border_width;
    y = widget->allocation.y + 
      (widget->allocation.height - indicator_size) / 2;
    
    child = GTK_BIN (check_button)->child;
    if (!interior_focus || !(child && GTK_WIDGET_VISIBLE (child)))
      x += focus_width + focus_pad;      
    
    if (toggle_button->inconsistent)
      shadow_type = GTK_SHADOW_ETCHED_IN;
    else if (toggle_button->active)
      shadow_type = GTK_SHADOW_IN;
    else
      shadow_type = GTK_SHADOW_OUT;
    
    if (button->activate_timeout || (toggle_button->active))
      state_type = GTK_STATE_ACTIVE;
    else if (button->in_button)
      state_type = GTK_STATE_PRELIGHT;
    else if (!GTK_WIDGET_IS_SENSITIVE (widget)){
      state_type = GTK_STATE_INSENSITIVE;
      shadow_type = GTK_SHADOW_ETCHED_IN;
    }else
      state_type = GTK_STATE_NORMAL;
    
    if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL)
      x = widget->allocation.x + widget->allocation.width - 
	(indicator_size + x - widget->allocation.x);
    
    draw_triangle (widget->style, widget->window,
		   state_type, shadow_type,
		   x, y, indicator_size);
  }
}


static void windowbutton_class_init (WindowbuttonClass *class){
  GtkCheckButtonClass *cb_class = (GtkCheckButtonClass*) class;
  parent_class = g_type_class_peek_parent (class);
  cb_class->draw_indicator = windowbutton_draw_indicator;
}

static void windowbutton_init (Windowbutton *r){
}

GType windowbutton_get_type (void){
  static GType m_type = 0;
  if (!m_type){
    static const GTypeInfo m_info={
      sizeof (WindowbuttonClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) windowbutton_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (Windowbutton),
      0,
      (GInstanceInitFunc) windowbutton_init,
      0
    };
    
    m_type = g_type_register_static (GTK_TYPE_CHECK_BUTTON, "Windowbutton", &m_info, 0);
  }

  return m_type;
}

GtkWidget* windowbutton_new (char *markup){
  return g_object_new (windowbutton_get_type (), 
		       "label", markup, 
		       "use_underline", TRUE, NULL);
}


