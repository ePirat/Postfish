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

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <gdk/gdkkeysyms.h>
#include "multibar.h"

static GdkBitmap *stipple=NULL;
static GdkBitmap *stippleB=NULL;

static float compute_dampening(float width, float target,float current,float delta,int zerodamp){
  float raw_delta=target-current;
  float testdelta=delta+(raw_delta*.2);

  if(target<0 && !zerodamp){
    if(current>0)
      raw_delta=target-current;
  }else if(current<0 && !zerodamp){
    raw_delta=target-current;
  }else if(raw_delta<0){
    if(delta>0){
      raw_delta=0.;
    }else{
      if(raw_delta<testdelta)raw_delta=testdelta;
    }
  }else{
    if(delta<0){
      raw_delta=0.;
    }else{
      if(raw_delta>testdelta)raw_delta=testdelta;
    }
  }
  return raw_delta;
}


/* call me roughly 10-20fps */
static void compute(Multibar *m,float *lowvals, float *highvals, int n, int draw){
  int i,j,xpad;
  GtkWidget *widget=GTK_WIDGET(m);
  float max=-400;
  int height=widget->allocation.height;
  int width=widget->allocation.width;

  /* figure out the x padding */
  if(m->thumbs<1){
    xpad=1;
  }else{
    xpad=height;
  }

  m->xpad=xpad;

  if(m->readout){
    if(n>m->bars){
      if(!m->bartrackers)
	m->bartrackers=calloc(n,sizeof(*m->bartrackers));
      else{
	m->bartrackers=realloc(m->bartrackers,
			       n*sizeof(*m->bartrackers));
	memset(m->bartrackers+m->bars,0,
	       sizeof(*m->bartrackers)*(n-m->bars));
      }
      
      for(i=m->bars;i<n;i++){
	m->bartrackers[i].pixelposlo=-400;
	m->bartrackers[i].pixelposhi=-400;
	m->bartrackers[i].pixeldeltalo=0;
	m->bartrackers[i].pixeldeltahi=0;
      }
      
      m->bars=n;
    }else if(n<m->bars)
      m->bars=n;
    
    for(i=0;i<n;i++)
      if(highvals[i]>max)max=highvals[i];
    
    if(m->clipdelay>0)
      m->clipdelay--;
    else
      m->clipdelay=0;
    
    if(m->peakdelay>0)
      m->peakdelay--;
    else{
      m->peakdelay=0;
      m->peakdelta--;
      m->peak+=m->peakdelta;
    }
    
    if(max>m->peak){
      m->peak=max;
      m->peakdelay=15*2; /* ~2 second hold */
      m->peakdelta=0;
    }
    
    if(draw){
      int *pixhi=alloca(n*sizeof(*pixhi));
      int *pixlo=alloca(n*sizeof(*pixlo));
      
      for(i=0;i<n;i++){
	pixlo[i]=-1;
	for(j=0;j<=m->labels;j++)
	  if(lowvals[i]>=m->levels[j]){
	    if(lowvals[i]<=m->levels[j+1]){
	      float del=(lowvals[i]-m->levels[j])/(m->levels[j+1]-m->levels[j]);
	      pixlo[i]=(j+del)/m->labels*(widget->allocation.width-xpad*2-1)+xpad;
	      break;
	    }else if(j==m->labels){
	      pixlo[i]=widget->allocation.width-xpad+1;
	    }
	  }else
	    break;
	
	pixhi[i]=pixlo[i];
	for(;j<=m->labels;j++)
	  if(highvals[i]>=m->levels[j]){
	    if(highvals[i]<=m->levels[j+1]){
	      float del=(highvals[i]-m->levels[j])/(m->levels[j+1]-m->levels[j]);
	      pixhi[i]=(j+del)/m->labels*(widget->allocation.width-xpad*2-1)+xpad;
	      break;
	    }else if(j==m->labels){
	      pixhi[i]=widget->allocation.width-xpad+1;
	    }
	  }else
	    break;
      }
      
      /* dampen movement according to setup */
      
      for(i=0;i<n;i++){
	float trackhi=m->bartrackers[i].pixelposhi;
	float tracklo=m->bartrackers[i].pixelposlo;
	float delhi=m->bartrackers[i].pixeldeltahi;
	float dello=m->bartrackers[i].pixeldeltalo;
	
	/* hi */
	delhi = compute_dampening(width-xpad*2-1,pixhi[i],trackhi,delhi,m->dampen_flags & ZERO_DAMP);
	
	if(pixhi[i]>trackhi){
	  if(m->dampen_flags & HI_ATTACK)pixhi[i]=trackhi+delhi;
	}else{
	  if(m->dampen_flags & HI_DECAY)pixhi[i]=trackhi+delhi;
	}
	m->bartrackers[i].pixelposhi=pixhi[i];
	m->bartrackers[i].pixeldeltahi=delhi;
	
	/* lo */
	dello = compute_dampening(width-xpad*2-1,pixlo[i],tracklo,dello,m->dampen_flags & ZERO_DAMP);
	if(pixlo[i]>tracklo){
	  if(m->dampen_flags & LO_ATTACK)pixlo[i]=tracklo+dello;
	}else{
	  if(m->dampen_flags & LO_DECAY)pixlo[i]=tracklo+dello;
	}
	m->bartrackers[i].pixelposlo=pixlo[i];
	m->bartrackers[i].pixeldeltalo=dello;
	
      }
    }
  }
}

static void draw(GtkWidget *widget,int n){
  int i,j,x=-1;
  Multibar *m=MULTIBAR(widget);
  int xpad=m->xpad,upad=2,lpad=2;
  int height=widget->allocation.height;
  GtkWidget *parent=gtk_widget_get_parent(widget);

  if(m->thumbs>0){
    int leftover=height-widget->requisition.height+3;
    if(leftover<height/4)
      lpad+=leftover;
    else
      lpad+=height/4;
  }

  if(!m->boxcolor){
    m->boxcolor=gdk_gc_new(m->backing);
    gdk_gc_copy(m->boxcolor,widget->style->black_gc);
  }
    
  if(m->readout && widget->state!=GTK_STATE_INSENSITIVE){
    /* draw the pixel positions */
    while(x<widget->allocation.width){
      int r=0xffff,g=0xffff,b=0xffff;
      GdkColor rgb={0,0,0,0};
      int next=widget->allocation.width;
      for(i=0;i<n;i++){
	if(m->bartrackers[i].pixelposlo>x && m->bartrackers[i].pixelposlo<next)
	  next=m->bartrackers[i].pixelposlo;
	if(m->bartrackers[i].pixelposhi>x && m->bartrackers[i].pixelposhi<next)
	  next=m->bartrackers[i].pixelposhi;
      }
      
      for(i=0;i<n;i++){
	if(m->bartrackers[i].pixelposlo<=x && m->bartrackers[i].pixelposhi>=next){
	  switch(i%8){
	  case 0:
	    r*=.65;
	    g*=.65;
	    b*=.65;
	    break;
	  case 1:
	    r*=1.;
	    g*=.5;
	    b*=.5;
	    break;
	  case 2:
	    r*=.6;
	    g*=.6;
	    b*=1.;
	    break;
	  case 3:
	    r*=.4;
	    g*=.9;
	    b*=.4;
	    break;
	  case 4:
	    r*=.7;
	    g*=.6;
	    b*=.3;
	    break;
	  case 5:
	    r*=.7;
	    g*=.4;
	    b*=.8;
	    break;
	  case 6:
	    r*=.3;
	    g*=.7;
	    b*=.7;
	    break;
	  case 7:
	    r*=.4;
	    g*=.4;
	    b*=.4;
	    break;
	  }
	}
      }
      rgb.red=r;
      rgb.green=g;
      rgb.blue=b;
      gdk_gc_set_rgb_fg_color(m->boxcolor,&rgb);
      
      gdk_draw_rectangle(m->backing,m->boxcolor,1,x+1,upad+1,next-x,widget->allocation.height-lpad-upad-3);
      
      x=next;
    }
    
    gdk_draw_line (m->backing,
		   widget->style->white_gc,
		   xpad, widget->allocation.height-lpad-1, 
		   widget->allocation.width-1-xpad, 
		   widget->allocation.height-lpad-1);
    
    if(m->clipdelay){
      gdk_draw_line (m->backing,
		     widget->style->fg_gc[1],
		     xpad, upad, widget->allocation.width-1-xpad, upad);
      
      gdk_draw_line (m->backing,
		     widget->style->fg_gc[1],
		     xpad, widget->allocation.height-lpad-2, 
		     widget->allocation.width-1-xpad, 
		     widget->allocation.height-lpad-2);
    }else{
      gdk_draw_line (m->backing,
		     widget->style->white_gc,
		     xpad, upad, widget->allocation.width-1-xpad, upad);
      
      gdk_draw_line (m->backing,
		     widget->style->white_gc,
		     xpad, widget->allocation.height-lpad-2, 
		     widget->allocation.width-1-xpad, 
		     widget->allocation.height-lpad-2);
    }
    
    /* peak follower */
    if(m->dampen_flags & PEAK_FOLLOW){
      int x=-10;
      for(j=0;j<=m->labels+1;j++)
	if(m->peak>=m->levels[j]){
	  if(m->peak<=m->levels[j+1]){
	    float del=(m->peak-m->levels[j])/(m->levels[j+1]-m->levels[j]);
	    x=(j+del)/m->labels*(widget->allocation.width-xpad*2-1)+xpad;
	    break;
	  }else if (j==m->labels){
	    x=widget->allocation.width-xpad+1;
	  }
	}else
	  break;
      
      for(j=0;j<n;j++)
	if(x<m->bartrackers[j].pixelposhi)
	  x=m->bartrackers[j].pixelposhi;
      
      {
	int y=widget->allocation.height-lpad-1;
	
	gdk_draw_line(m->backing,widget->style->fg_gc[0],
		      x-3,upad,x+3,upad);
	gdk_draw_line(m->backing,widget->style->fg_gc[0],
		      x-2,upad+1,x+2,upad+1);
	gdk_draw_line(m->backing,widget->style->fg_gc[0],
		      x-1,upad+2,x+1,upad+2);
	gdk_draw_line(m->backing,widget->style->fg_gc[0],
		      x-3,y-1,x+3,y-1);
	gdk_draw_line(m->backing,widget->style->fg_gc[0],
		      x-2,y-2,x+2,y-2);
	gdk_draw_line(m->backing,widget->style->fg_gc[0],
		      x-1,y-3,x+1,y-3);
	
	gdk_draw_line(m->backing,widget->style->fg_gc[1],
		      x,upad+1,x,y-2);
      }
    }
  }else{

    int width=widget->allocation.width-xpad;
    int height=widget->allocation.height;
    GdkGC *gc=parent->style->bg_gc[0];

    /* blank scale to bg of parent */
    gdk_draw_rectangle(m->backing,gc,1,xpad,0,width-xpad+1,height-lpad);

  }

  for(i=0;i<=m->labels;i++){
    int x=rint(((float)i)/m->labels*(widget->allocation.width-xpad*2-1))+xpad;
    int y=widget->allocation.height-lpad-upad;
    int px,py;
    int gc=0;
    
    if(m->levels[i]>0.)gc=1;
    
    if(m->readout)
      gdk_draw_line (m->backing,
		     widget->style->text_gc[gc],
		     x, upad, x, y+upad);
    else
      gdk_draw_line (m->backing,
		     widget->style->text_gc[gc],
		     x, y/4+upad, x, y+upad);

    pango_layout_get_pixel_size(m->layout[i],&px,&py);

    if(i==0){
      x+=2;
      y-=py;
      y/=2;
    }else{
      x-=px+2;
      y-=py;
      y/=2;
    }
    gdk_draw_layout (m->backing,
		     widget->style->text_gc[gc],
		     x, y+upad,
		     m->layout[i]);

  }

  /* draw frame */
  {
    int width=widget->allocation.width;
    int height=widget->allocation.height;
    GdkGC *gc=parent->style->bg_gc[0];
    GdkGC *light_gc=parent->style->light_gc[0];
    GdkGC *dark_gc=parent->style->dark_gc[0];
    GdkGC *mid_gc=widget->style->bg_gc[GTK_STATE_ACTIVE];

    /* blank side padding to bg of parent */
    gdk_draw_rectangle(m->backing,gc,1,0,0,xpad,height);
    gdk_draw_rectangle(m->backing,gc,1,width-xpad,0,xpad,height);

    /* blank sides of trough */
    gdk_draw_rectangle(m->backing,gc,1,
		       0,
		       height-lpad,
		       m->thumblo_x,
		       lpad);
    gdk_draw_rectangle(m->backing,gc,1,
		       m->thumbhi_x+xpad+xpad,
		       height-lpad,
		       width-xpad-xpad-m->thumbhi_x,
		       lpad);
    
    /* frame */
    if(m->readout){
      gdk_draw_line(m->backing,dark_gc,0,0,width-2,0);
      gdk_draw_line(m->backing,dark_gc,0,0,0,height-lpad);
      gdk_draw_line(m->backing,dark_gc,1,height-lpad,width-2,height-lpad);
      gdk_draw_line(m->backing,dark_gc,width-2,height-lpad,width-2,1);
      
      gdk_draw_line(m->backing,light_gc,0,height-lpad+1,
		    width-1,height-lpad+1);
      gdk_draw_line(m->backing,light_gc,width-1,0,width-1,height-lpad+1);
      gdk_draw_line(m->backing,light_gc,1,1,width-3,1);
      gdk_draw_line(m->backing,light_gc,1,1,1,height-lpad-1);
    }


    /* dark trough */
    if(lpad>2 || m->readout==0){
      if(lpad>2){
	gdk_draw_rectangle(m->backing,mid_gc,1,
			   m->thumblo_x+1,height-lpad+1,
			   m->thumbhi_x-m->thumblo_x+xpad*2,lpad-1);
	
	gdk_draw_line(m->backing,dark_gc,
		      m->thumblo_x,height-lpad,
		      m->thumblo_x,height-1);
	
      }

      gdk_draw_line(m->backing,light_gc,
		    m->thumblo_x,height-1,
		    m->thumbhi_x+xpad*2,height-1);

      dark_gc=widget->style->dark_gc[GTK_STATE_ACTIVE];

      gdk_draw_line(m->backing,dark_gc,
		    m->thumblo_x,height-lpad,
		    m->thumbhi_x+xpad*2-1,height-lpad);
      
      if(lpad>2)
	gdk_draw_line(m->backing,light_gc,
		      m->thumbhi_x+xpad*2,height-1,
		      m->thumbhi_x+xpad*2,height-lpad+1);


      if(m->readout==0)
	gdk_draw_point(m->backing,light_gc,width-1,height-lpad);
    }
      

  }

  /* draw slider thumbs */
  if(m->thumbs){
    int height=widget->allocation.height,i,j;
    GdkGC *black_gc=widget->style->black_gc;
    int y0=height/3-1;
    int y1=height-3;

    int outer=height-1;
    int inner=(y1+y0)/2-y0;

    int A[3]={outer,outer,outer};
    int B[3]={inner,inner,inner};
    int C[3]={inner,inner,inner};
    int D[3]={outer,outer,outer};

    GdkColor yellow={0,0xff00,0xd000,0};

    if(m->thumbs==2){
      /* adjustment required for overlapping thumbs? */
      int mid=m->thumbpixel[1]-m->thumbpixel[0]-1;
      int midA=(mid<0?0:mid)/2;
      int midB=(mid<0?0:mid)-midA;
      
      if(midA<D[0])D[0]=midA;
      if(midA<C[0])C[0]=midA;
      if(midB<A[1])A[1]=midB;
      if(midB<B[1])B[1]=midB;
    }

    if(m->thumbs==3){
      /* adjust for 0,1 overlap if any; different from the 2 case */
      int mid=m->thumbpixel[1]-m->thumbpixel[0]-1;
      int midA=(mid<0?0:mid)/2;
      int midB=(mid<0?0:mid)-midA;

      if(midA<D[0])D[0]=midA;
      if(D[0]<C[0]+2)D[0]=C[0]+2;
      if(midB<A[1])A[1]=midB;
      if(A[1]<B[1]+2)A[1]=B[1]+2;

      /* adjust for 1,2 overlap if any */

      mid=m->thumbpixel[2]-m->thumbpixel[1]-1;
      midA=(mid<0?0:mid)/2;
      midB=(mid<0?0:mid)-midA;

      if(midA<D[1])D[1]=midA;
      if(D[1]<C[1]+2)D[1]=C[1]+2;
      if(midB<A[2])A[2]=midB;
      if(A[2]<B[2]+2)A[2]=B[2]+2;
    }
    
    for(i=0;i<m->thumbs;i++){
      if(m->thumbs==3){
	/* in the three-thumb case, the middle thumb is drawn last */
	switch(i){
	case 0:
	  j=0;
	  break;
	case 1:
	  j=2;
	  break;
	case 2:
	  j=1;
	  break;
	}
      }else
	j=i;
      
      {
	int x=m->thumbpixel[j]+xpad;
	GdkPoint p[8]={ {x+D[j],y0+C[j]},
			{x+C[j],y0+C[j]}, 
			{x     ,y0     },
			{x-B[j],y0+B[j]},
			{x-A[j],y0+B[j]},
			{x-A[j],y1+1   },
			{x+D[j],y1+1   },
			{x+D[j],y0+C[j]}};
	
	GdkPoint d[3]={ {x-A[j]+1,y1},
			{x+D[j]-1,y1},
			{x+D[j]-1,y0+C[j]+1}};
	
	GdkGC *gc=widget->style->bg_gc[m->thumbstate[j]];
	GdkGC *light_gc=widget->style->light_gc[m->thumbstate[j]];
	GdkGC *dark_gc=widget->style->dark_gc[m->thumbstate[j]];
      
	gdk_draw_polygon(m->backing,gc,TRUE,p,7);
	gdk_draw_lines(m->backing,dark_gc,d,3);
      
	if(m->thumbfocus==j){
	  if(x&1)
	    gdk_gc_set_stipple(black_gc,stipple);
	  else
	    gdk_gc_set_stipple(black_gc,stippleB);

	  gdk_gc_set_fill(black_gc,GDK_STIPPLED);
	  gdk_draw_polygon(m->backing,black_gc,TRUE,p,7);
	  gdk_gc_set_fill(black_gc,GDK_SOLID);
	}
      
	gdk_draw_lines(m->backing,light_gc,p,6);
	gdk_draw_lines(m->backing,black_gc,p+5,3);
      
	gdk_gc_set_rgb_fg_color(m->boxcolor,&yellow);
	gdk_draw_line(m->backing,m->boxcolor,x,y1-1,x,0);
      }
    }
  }
}

static void draw_and_expose(GtkWidget *widget){
  Multibar *m=MULTIBAR(widget);
  if(!GDK_IS_DRAWABLE(m->backing))return;
  draw(widget,m->bars);
  if(!GTK_WIDGET_DRAWABLE(widget))return;
  if(!GDK_IS_DRAWABLE(widget->window))return;
  gdk_draw_drawable(widget->window,
		    widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
		    m->backing,
		    0, 0,
		    0, 0,
		    widget->allocation.width,		  
		    widget->allocation.height);
}

static gboolean expose( GtkWidget *widget, GdkEventExpose *event ){
  Multibar *m=MULTIBAR(widget);
  gdk_draw_drawable(widget->window,
		    widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
		    m->backing,
		    event->area.x, event->area.y,
		    event->area.x, event->area.y,
		    event->area.width, event->area.height);
  
  return FALSE;
}

static void size_request (GtkWidget *widget,GtkRequisition *requisition){
  int i,maxx=0,maxy=0,x,y,xpad;
  Multibar *m=MULTIBAR(widget);

  for(i=0;i<=m->labels;i++){
    pango_layout_get_pixel_size(m->layout[i],&x,&y);

    if(x>maxx)maxx=x;
    if(y>maxy)maxy=y;
  }

  maxy+=4;

  if(m->thumbs==0){
    xpad=1;
  }else{
    maxy+=3;
    xpad=maxy;
  }

  requisition->width = (maxx*1.5+2)*m->labels+xpad*2;
  requisition->height = maxy;

}

static int transition_thumbfocus=0;
static gboolean multibar_focus (GtkWidget         *widget,
				GtkDirectionType   direction){
  Multibar *m=MULTIBAR(widget);
  int ret=TRUE;

  if(m->thumbs==0)return FALSE;

  switch(direction){
  case GTK_DIR_TAB_FORWARD:
  case GTK_DIR_RIGHT:
    if(m->thumbfocus+1>=m->thumbs){
      m->thumbfocus=-1;
      ret=FALSE;
    }else
      m->thumbfocus++;
    break;

  case GTK_DIR_TAB_BACKWARD:
  case GTK_DIR_LEFT:
    if(m->thumbfocus==-1)
      m->thumbfocus=m->thumbs-1;
    else{
      if(m->thumbfocus-1<0){
	m->thumbfocus=-1;
	ret=FALSE;
      }else
	m->thumbfocus--;
    }
    break;

  case GTK_DIR_UP:
  case GTK_DIR_DOWN:
    if(m->thumbfocus==-1){
      if(transition_thumbfocus>=0 && transition_thumbfocus<m->thumbs)
	m->thumbfocus=transition_thumbfocus;
      else
	m->thumbfocus=0;
      ret=TRUE;
    }else{
      transition_thumbfocus=m->thumbfocus;
      ret=FALSE;
    }
    break;

  default:
    ret=FALSE;
  }

  m->prev_thumbfocus=m->thumbfocus;
  if(ret==TRUE) gtk_widget_grab_focus(widget);
  draw_and_expose(widget);

  return ret;
}

static gint determine_thumb(Multibar *m,int ix, int iy){
  GtkWidget *widget=GTK_WIDGET(m);
  int height=widget->allocation.height;
  float distances[3]={-1,-1,-1};
  int thumb=-1;

  /* lower thumb */
  if(m->thumbs==1 || m->thumbs>2){
    int num=(m->thumbs==1?0:1);

    int x= ix-m->thumbpixel[num];
    int y= iy-(height*4/5-2);
    distances[num]=sqrt(x*x + y*y);
  }

  /* left thumb */
  if(m->thumbs>1){
    int x= ix-(m->thumbpixel[0]-(height/2));
    int y= iy-(height/2-3);
    distances[0]=sqrt(x*x + y*y);
  }
  
  /* right thumb */
  if(m->thumbs>1){
    int num=(m->thumbs==2?1:2);
    int x= ix-(m->thumbpixel[num]+(height/2));
    int y= iy-(height/2-3);
    distances[num]=sqrt(x*x + y*y);
  }
  
  if(m->thumbs && distances[0]<height)thumb=0;
  if(m->thumbs>1 && distances[1]<height)
    if(thumb == -1 || distances[1]<distances[0])thumb=1;
  if(m->thumbs>2 && distances[2]<height)
    if(thumb == -1 || (distances[2]<distances[0] && 
		       distances[2]<distances[1]))thumb=2;
  return thumb;
}

static int pixel_bound(Multibar *m,int x){
  GtkWidget *w=GTK_WIDGET(m);
  if(x<0)return 0;
  if(x>w->allocation.width-m->xpad*2-1)
    return w->allocation.width-m->xpad*2-1;
  return x;
}

static float pixel_to_val(Multibar *m,int x){
  GtkWidget *w=GTK_WIDGET(m);
  int j;

  for(j=0;j<=m->labels;j++){
    int pixlo=rint((float)j/m->labels*(w->allocation.width-m->xpad*2-1));
    int pixhi=rint((float)(j+1)/m->labels*(w->allocation.width-m->xpad*2-1));

    if(x>=pixlo && x<=pixhi){
      if(pixlo==pixhi)return m->levels[j];
      float del=(float)(x-pixlo)/(pixhi-pixlo);
      return (1.-del)*m->levels[j] + del*m->levels[j+1];
    }
  }
  return 0.;
}

static int val_to_pixel(Multibar *m,float v){
  GtkWidget *w=GTK_WIDGET(m);
  int j,ret=0;

  if(v<m->levels[0]){
    ret=0;
  }else if(v>m->levels[m->labels]){
    ret=w->allocation.width-m->xpad*2-1;
  }else{
    for(j=0;j<=m->labels;j++){
      if(v>=m->levels[j] && v<=m->levels[j+1]){
	float del=(v-m->levels[j])/(m->levels[j+1]-m->levels[j]);
	int pixlo=rint((float)j/m->labels*(w->allocation.width-m->xpad*2-1));
	int pixhi=rint((float)(j+1)/m->labels*(w->allocation.width-m->xpad*2-1));
	ret=rint(pixlo*(1.-del)+pixhi*del);
	break;
      }
    }
  }

  ret=pixel_bound(m,ret);
  return ret;
}

static gboolean configure(GtkWidget *widget, GdkEventConfigure *event){
  Multibar *m=MULTIBAR(widget);
  int i;
  
  if (m->backing)
    g_object_unref(m->backing);
  
  m->backing = gdk_pixmap_new(widget->window,
			      widget->allocation.width,
			      widget->allocation.height,
			      -1);
  gdk_draw_rectangle(m->backing,widget->style->white_gc,1,0,0,widget->allocation.width,
		     widget->allocation.height);
  
  compute(m,0,0,0,1);
  for(i=0;i<m->thumbs;i++)
    m->thumbpixel[i]=val_to_pixel(m,m->thumbval[i]);
  m->thumblo_x=val_to_pixel(m,m->thumblo);
  m->thumbhi_x=val_to_pixel(m,m->thumbhi);

  draw_and_expose(widget);

  return TRUE;
}

static void vals_bound(Multibar *m){
  int i;

  if(m->thumbsmall>0 && m->thumblarge>0)
    for(i=0;i<m->thumbs;i++)
      m->thumbval[i]=rint(m->thumbval[i]/m->thumbsmall)*m->thumbsmall;

  for(i=0;i<m->thumbs;i++){
    if(m->thumbval[i]<m->thumblo)m->thumbval[i]=m->thumblo;
    if(m->thumbval[i]>m->thumbhi)m->thumbval[i]=m->thumbhi;
    m->thumbpixel[i]=val_to_pixel(m,m->thumbval[i]);
  }

  if(m->thumbfocus>=0){
    float v=m->thumbval[m->thumbfocus];
    int    x=m->thumbpixel[m->thumbfocus];
    
    if(m->thumbfocus==2){
      if(m->thumbpixel[1]>x){
	m->thumbpixel[1]=x;
	m->thumbval[1]=v;
      }
      if(m->thumbpixel[0]>x){
	m->thumbpixel[0]=x;
	m->thumbval[0]=v;
      }
    }
    
    if(m->thumbfocus==1){
      if(m->thumbpixel[2]<x){
	m->thumbpixel[2]=x;
	m->thumbval[2]=v;
      }
      if(m->thumbpixel[0]>x){
	m->thumbpixel[0]=x;
	m->thumbval[0]=v;
      }
    }
    
    if(m->thumbfocus==0){
      if(m->thumbpixel[2]<x){
	m->thumbpixel[2]=x;
	m->thumbval[2]=v;
      }
      if(m->thumbpixel[1]<x){
	m->thumbpixel[1]=x;
	m->thumbval[1]=v;
      }
    }
  }

}

static gint multibar_motion(GtkWidget        *w,
			    GdkEventMotion   *event){
  Multibar *m=MULTIBAR(w);

  /* is a thumb already grabbed? */
  if(m->thumbgrab>=0){
    
    int x=event->x+m->thumbx;
    float v;

    x=pixel_bound(m,x);
    m->thumbval[m->thumbgrab]=pixel_to_val(m,x);
    vals_bound(m);
    v=m->thumbval[m->thumbgrab];
    x=m->thumbpixel[m->thumbgrab]=val_to_pixel(m,v);

    if(m->callback)m->callback(GTK_WIDGET(m),m->callbackp);
    draw_and_expose(w);

  }else{
    /* nothing grabbed right now; determine if we're in a a thumb's area */
    int thumb=determine_thumb(m,event->x-m->xpad,event->y);
    GtkStateType thumbstate[3];
    thumbstate[0]=GTK_STATE_NORMAL;
    thumbstate[1]=GTK_STATE_NORMAL;
    thumbstate[2]=GTK_STATE_NORMAL;
    if(thumb>=0)thumbstate[thumb]=GTK_STATE_PRELIGHT;

    if(thumbstate[0]!=m->thumbstate[0] ||
       thumbstate[1]!=m->thumbstate[1] ||
       thumbstate[2]!=m->thumbstate[2]){
      m->thumbstate[0]=thumbstate[0];
      m->thumbstate[1]=thumbstate[1];
      m->thumbstate[2]=thumbstate[2];

      draw_and_expose(w);
    }
  }
  return TRUE;
}

static gint multibar_leave(GtkWidget        *widget,
			   GdkEventCrossing *event){
  Multibar *m=MULTIBAR(widget);

  if(m->thumbgrab<0){
    if(0!=m->thumbstate[0] ||
       0!=m->thumbstate[1] ||
       0!=m->thumbstate[2]){
      m->thumbstate[0]=0;
      m->thumbstate[1]=0;
      m->thumbstate[2]=0;
      
      draw_and_expose(widget);
    }
  }
  return TRUE;
}

static gboolean button_press   (GtkWidget        *widget,
			    GdkEventButton   *event){
  Multibar *m=MULTIBAR(widget);
  if(m->thumbstate[0]){
    gtk_widget_grab_focus(widget);
    transition_thumbfocus=m->thumbgrab=m->thumbfocus=0;
    m->thumbx=m->thumbpixel[0]-event->x;
  }else if(m->thumbstate[1]){
    gtk_widget_grab_focus(widget);
    transition_thumbfocus=m->thumbgrab=m->thumbfocus=1;
    m->thumbx=m->thumbpixel[1]-event->x;
  }else if(m->thumbstate[2]){
    gtk_widget_grab_focus(widget);
    transition_thumbfocus=m->thumbgrab=m->thumbfocus=2;
    m->thumbx=m->thumbpixel[2]-event->x;
  }
  draw_and_expose(widget);
  return TRUE;
}

static gboolean button_release   (GtkWidget        *widget,
			    GdkEventButton   *event){
  Multibar *m=MULTIBAR(widget);
  m->thumbgrab=-1;
  draw_and_expose(widget);
  return TRUE;
}

static gboolean unfocus(GtkWidget        *widget,
			GdkEventFocus       *event){
  Multibar *m=MULTIBAR(widget);
  m->prev_thumbfocus=m->thumbfocus;
  m->thumbfocus=-1;
  draw_and_expose(widget);
  return TRUE;
}

static gboolean refocus(GtkWidget        *widget,
			GdkEventFocus       *event){
  Multibar *m=MULTIBAR(widget);
  transition_thumbfocus=m->thumbfocus=m->prev_thumbfocus;
  m->thumbgrab=-1;
  draw_and_expose(widget);
  return TRUE;
}

gboolean key_press(GtkWidget *w,GdkEventKey *event){
  Multibar *m=MULTIBAR(w);
  int x;
  if(event->state&GDK_MOD1_MASK) return FALSE;
  if(event->state&GDK_CONTROL_MASK) return FALSE;

  if(m->thumbfocus>=0){
    if(m->thumbsmall>0 && m->thumblarge>0){

      switch(event->keyval){
      case GDK_minus:
	m->thumbval[m->thumbfocus]-=m->thumbsmall;
	break;
      case GDK_underscore:
	m->thumbval[m->thumbfocus]-=m->thumblarge;
	break;
      case GDK_equal:
	m->thumbval[m->thumbfocus]+=m->thumbsmall;
	break;
      case GDK_plus:
	m->thumbval[m->thumbfocus]+=m->thumblarge;
	break;
      default:
	return FALSE;
      }

      vals_bound(m);
      x=val_to_pixel(m,m->thumbval[m->thumbfocus]);
      x=pixel_bound(m,x);
      m->thumbpixel[m->thumbfocus]=x;

    }else{

      switch(event->keyval){
      case GDK_minus:
	x=m->thumbpixel[m->thumbfocus]-1;
	break;
      case GDK_underscore:
	x=m->thumbpixel[m->thumbfocus]-10;
	break;
      case GDK_equal:
	x=m->thumbpixel[m->thumbfocus]+1;
	break;
      case GDK_plus:
	x=m->thumbpixel[m->thumbfocus]+10;
	break;
      default:
	return FALSE;
      }

      x=pixel_bound(m,x);
      m->thumbpixel[m->thumbfocus]=x;
      m->thumbval[m->thumbfocus]=pixel_to_val(m,x);
      vals_bound(m);

    }

    if(m->callback)m->callback(GTK_WIDGET(m),m->callbackp);

    draw_and_expose(w);
    return TRUE;
  }
  return FALSE;
}


static GtkDrawingAreaClass *parent_class = NULL;


static void state_changed(GtkWidget *w,GtkStateType ps){
  draw_and_expose(w);
}

static void multibar_class_init (MultibarClass *class){
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);
  parent_class = g_type_class_peek_parent (class);

  widget_class->expose_event = expose;
  widget_class->configure_event = configure;
  widget_class->size_request = size_request;
  widget_class->focus=multibar_focus;
  widget_class->key_press_event = key_press;
  widget_class->button_press_event = button_press;
  widget_class->button_release_event = button_release;
  widget_class->leave_notify_event = multibar_leave;
  widget_class->motion_notify_event = multibar_motion;
  widget_class->focus_out_event = unfocus;
  widget_class->focus_in_event = refocus;
  widget_class->state_changed = state_changed;

  stipple=gdk_bitmap_create_from_data(NULL,"\125\352",2,2);
  stippleB=gdk_bitmap_create_from_data(NULL,"\352\125",2,2);

}

static void multibar_init (Multibar *m){
  m->layout=0;
  m->peakdelay=0;
  m->clipdelay=0;
  m->peak=-400;
}

GType multibar_get_type (void){
  static GType m_type = 0;
  if (!m_type){
    static const GTypeInfo m_info={
      sizeof (MultibarClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) multibar_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (Multibar),
      0,
      (GInstanceInitFunc) multibar_init,
      0
    };
    
    m_type = g_type_register_static (GTK_TYPE_DRAWING_AREA, "Multibar", &m_info, 0);
  }

  return m_type;
}

GtkWidget* multibar_new (int n, char **labels, float *levels, int thumbs,
			 int flags){
  int i;
  GtkWidget *ret= GTK_WIDGET (g_object_new (multibar_get_type (), NULL));
  Multibar *m=MULTIBAR(ret);

  /* not the *proper* way to do it, but this is a one-shot */
  m->levels=calloc((n+1),sizeof(*m->levels));
  m->labels=n-1;
  memcpy(m->levels,levels,n*sizeof(*levels));

  m->layout=calloc(n,sizeof(*m->layout));
  for(i=0;i<n;i++)
    m->layout[i]=gtk_widget_create_pango_layout(ret,labels[i]);

  m->dampen_flags=flags;
  m->thumbfocus=-1;
  m->thumbgrab=-1;
  m->thumblo=levels[0];
  m->thumbhi=levels[n-1];
  m->thumblo_x=val_to_pixel(m,m->thumblo);
  m->thumbhi_x=val_to_pixel(m,m->thumbhi);

  if(thumbs<0)thumbs=0;
  if(thumbs>3)thumbs=3;
  m->thumbs=thumbs;
  if(m->thumbs!=0)  GTK_WIDGET_SET_FLAGS (m, GTK_CAN_FOCUS);

  {
    int events=gtk_widget_get_events(ret);
    gtk_widget_set_events(ret,              events|
			  GDK_POINTER_MOTION_MASK|
			  GDK_BUTTON_PRESS_MASK	 |
			  GDK_BUTTON_RELEASE_MASK|
			  GDK_KEY_PRESS_MASK     |
			  GDK_KEY_RELEASE_MASK   |
			  GDK_ENTER_NOTIFY_MASK  |
			  GDK_LEAVE_NOTIFY_MASK  |
			  GDK_FOCUS_CHANGE_MASK  );
  }

  m->readout=1;
  return ret;
}

GtkWidget* multibar_slider_new (int n, char **labels, float *levels, 
				int thumbs){
  GtkWidget *ret= multibar_new(n,labels,levels,thumbs,0);
  Multibar *m=MULTIBAR(ret);
  m->readout=0;
  gtk_widget_set_name(ret,"Multislide");

  return ret;
}

void multibar_set(Multibar *m,float *lo, float *hi, int n, int draw){
  GtkWidget *widget=GTK_WIDGET(m);
  compute(m,lo,hi,n,draw);
  if(draw)draw_and_expose(widget);
}

void multibar_thumb_set(Multibar *m,float v, int n){
  GtkWidget *w=GTK_WIDGET(m);
  int x;

  if(n<0)return;
  if(n>=m->thumbs)return;

  {
    m->thumbval[n]=v;
    vals_bound(m);
    v=m->thumbval[n];
    x=m->thumbpixel[n]=val_to_pixel(m,v);
    m->thumbval[n]=v;
  
    if(n==0){
      if(m->thumbpixel[1]<x){
	m->thumbval[1]=v;
	m->thumbpixel[1]=x;
      }
      if(m->thumbpixel[2]<x){
	m->thumbval[2]=v;
	m->thumbpixel[2]=x;
      }
    }

    if(n==1){
      if(m->thumbpixel[0]>x){
	m->thumbval[0]=v;
	m->thumbpixel[0]=x;
      }
      if(m->thumbpixel[2]<x){
	m->thumbval[2]=v;
	m->thumbpixel[2]=x;
      }
    }

    if(n==2){
      if(m->thumbpixel[0]>x){
	m->thumbval[0]=v;
	m->thumbpixel[0]=x;
      }
      if(m->thumbpixel[1]>x){
	m->thumbval[1]=v;
	m->thumbpixel[1]=x;
      }
    }
  }
  if(m->callback)m->callback(GTK_WIDGET(m),m->callbackp);
  draw_and_expose(w);
}

void multibar_reset(Multibar *m){
  m->peak=-400;
  m->peakdelta=0;
  m->peakdelay=0;
  m->clipdelay=0;
  multibar_set(m,NULL,NULL,0,1);
}

void multibar_setwarn(Multibar *m,int draw){
  GtkWidget *widget=GTK_WIDGET(m);
  if(!m->clipdelay){
    m->clipdelay=15*10;

    if(draw)draw_and_expose(widget);
  }else
    m->clipdelay=15*10; /* ~ ten second hold */
}

/* because closures are ludicrously complex for doing something this simple */
void multibar_callback(Multibar *m,void (*callback)(GtkWidget *,gpointer),
		       gpointer p){
  m->callback=callback;
  m->callbackp=p;
}
  
float multibar_get_value(Multibar *m,int n){
  if(n<0)return 0.;
  if(n>m->thumbs)return 0.;
  return m->thumbval[n];
}

void multibar_thumb_bounds(Multibar *m,float lo, float hi){
  GtkWidget *w=GTK_WIDGET(m);
  if(lo>hi)return;

  if(lo<m->levels[0])lo=m->levels[0];
  if(hi<m->levels[0])hi=m->levels[0];
  if(lo>m->levels[m->labels])lo=m->levels[m->labels];
  if(hi>m->levels[m->labels])hi=m->levels[m->labels];

  m->thumblo=lo;
  m->thumbhi=hi;

  m->thumblo_x=val_to_pixel(m,lo);
  m->thumbhi_x=val_to_pixel(m,hi);

  vals_bound(m);
  if(m->callback)m->callback(GTK_WIDGET(m),m->callbackp);
  draw_and_expose(w);
}

void multibar_thumb_increment(Multibar *m,float small, float large){
  GtkWidget *w=GTK_WIDGET(m);
  if(small>large)return;

  m->thumbsmall=small;
  m->thumblarge=large;

  vals_bound(m);
  if(m->callback)m->callback(GTK_WIDGET(m),m->callbackp);
  draw_and_expose(w);
}

