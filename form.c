/*
 *
 *  form.c
 *    
 *      Copyright (C) 2002 Monty
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

/* This project is a small, tightly tailored application.  it is not
   designed to be nigh-infinitely extensible, nor is it designed to be
   reusable code.  It's monolithic, inflexible, and designed that way
   on purpose. */

#include <stdlib.h>
#include <ncurses.h>
#include <pthread.h>
#include <math.h>
#include <form.h>

static int cursor_active=0;
static int cursor_x;
static int cursor_y;

extern pthread_mutex_t master_mutex;

long pow10(int power){
  long ret=1,i;
  for(i=0;i<power;i++)
    ret*=10;
  return(ret);
}

void *m_realloc(void *in,int bytes){
  if(!in)
    return(malloc(bytes));
  return(realloc(in,bytes));
}

void addnlstr(const char *s,int n,char c){
  int len=strlen(s),i;
  addnstr(s,n);
  n-=len;
  for(i=0;i<n;i++)addch(c);
}

void switch_to_stderr(){
  def_prog_mode();           /* save current tty modes */
  endwin();                  /* restore original tty modes */
}

void switch_to_ncurses(){
  refresh();                 /* restore save modes, repaint screen */
}

int mgetch(){
  while(1){
    int ret=getch();
    if(ret>0)return(ret);
  }
}

int pgetch(){
  while(1){
    int ret;

    if(cursor_active){
      move(cursor_y,cursor_x);
      curs_set(1);
    }else{
      curs_set(0);
    }
    
    ret=getch();
    if(ret>=0)return(ret);
  }
}

void form_init(form *f,int maxstorage,int editable){
  memset(f,0,sizeof(*f));
  f->fields=calloc(maxstorage,sizeof(formfield));
  f->editable=editable;
  f->storage=maxstorage;
}

void form_clear(form *f){
  if(f->fields)free(f->fields);
  memset(f,0,sizeof(*f));
}

void draw_field(formfield *f){
  int y,x;
  int i;
  long lval;
  long focus=(f->form->fields+f->form->cursor==f?1:0);
  getyx(stdscr,y,x);
  move(f->y,f->x);

  if(f->form->editable && f->active){
    if(focus){
      attron(A_REVERSE);
    }else{
      attron(A_BOLD);
    }
  }
  
  pthread_mutex_lock(&master_mutex);
  lval=*(long *)(f->var);
  pthread_mutex_unlock(&master_mutex);

  switch(f->type){
  case FORM_TIME:
    {
      long mult=pow10(f->editwidth-1);
      int zeroflag=0;
      int count=0;
      /*xxxHHHHH:MM:SS.HH*/
      /*       9876543210*/
      for(i=f->width-1;i>=0;i--){
	
	switch(i){
	case 2:
	  addch('.');
	  break;
	case 5: case 8:
	  addch(':');
	  break;
	default:
	  if(f->form->editable && focus && count==f->cursor)
	    attroff(A_REVERSE);

	  if(f->active){
	    if(lval!=-1 && ((lval/mult)%10 || i<5)){
	      zeroflag=1;
	      addch(48+(lval/mult)%10);
	    }else{
	      if(zeroflag)
		addch('0');
	      else
		addch(' ');
	    }
	    mult/=10;
	    if(f->form->editable && focus && count==f->cursor)
	      attron(A_REVERSE);
	  }else{
	    addch('-');
	  }
	  count++;
	}
      }

      /* cursor? */
      if(focus){
	int val=f->editwidth-f->cursor-1;
	cursor_active=1;
	cursor_y=f->y;
	switch(val){
	case 0:case 1:
	  cursor_x=f->x+f->width-val-1;
	  break;
	case 2:case 3:
	  cursor_x=f->x+f->width-val-2;
	  break;
	case 4:case 5:
	  cursor_x=f->x+f->width-val-3;
	  break;
	default:
	  cursor_x=f->x+f->width-val-4;
	  break;
	}
      }
    }
    break;
  case FORM_P2:
    {
      char buf[80];
      int i;
      if(f->active){
	snprintf(buf,80,"%*ld",f->width,lval);
	addstr(buf);
      }else
	for(i=0;i<f->width;i++)addch('-');
      if(focus)cursor_active=0;
    }
    break;
  default:
    {
      char buf[80];
      if(f->active){
	if(f->dpoint)
	  snprintf(buf,80,"%+*.1f",f->width,lval*.1);
	else
	  snprintf(buf,80,"%+*ld",f->width,lval);
	addstr(buf);
      }else
	for(i=0;i<f->width;i++)addch('-');
      if(focus)cursor_active=0;
    }
    break;
  }
  
  attrset(0);

}

void form_redraw(form *f){
  int i;
  for(i=0;i<f->count;i++)
    draw_field(f->fields+i);
}

void field_active(formfield *f,int activep){
  f->active=activep;
  draw_field(f);
}

formfield *field_add(form *f,enum field_type type,int x,int y,int width,
		     void *var,void (*cb)(void),int d,int min, int max){
  int n=f->count;
  if(f->storage==n)return(NULL);
  if(width<1)return(NULL);
  /* add the struct, then draw contents */
  f->fields[n].type=type;
  f->fields[n].x=x;
  f->fields[n].y=y;
  f->fields[n].width=width;
  f->fields[n].var=var;
  f->fields[n].cb=cb;
  f->fields[n].dpoint=d;
  f->fields[n].active=1;

  f->fields[n].min=min;
  f->fields[n].max=max;

  f->fields[n].form=f;

  switch(type){
  case FORM_TIME:
    switch(width){
    case 1: case 2:
      f->fields[n].editwidth=width;
      break;
    case 3: case 4:
      f->fields[n].editwidth=width-1;
      break;
    case 5: case 6:
      f->fields[n].editwidth=width-2;
      break;
    default:
      f->fields[n].editwidth=width-3;
      break;
    }
    break;
  default:
    f->fields[n].editwidth=width;
  }
  
  f->count++;
  
  draw_field(f->fields+n);
  return(f->fields+n);
}

void form_next_field(form *f){
  int v=f->cursor;
  int t=(v+1>=f->count?v+1:0);
  while(t!=v && !f->fields[t].active)
    t=(t+1>=f->count?t+1:0);
  
  f->cursor=t;
  draw_field(f->fields+v);
  draw_field(f->fields+t);
}

void form_prev_field(form *f){
  int v=f->cursor;
  int t=(v-1<0?f->count-1:v-1);
  while(t!=v && !f->fields[t].active)
    t=(t-1<0?f->count-1:t-1);
  
  f->cursor=t;
  draw_field(f->fields+v);
  draw_field(f->fields+t);
}

void form_left_field(form *f){
  int temp=f->cursor;
  int i;
  double dist=99999;
  int best=f->cursor;
  int x=f->fields[f->cursor].x;
  int y=f->fields[f->cursor].y;
  for(i=0;i<f->count;i++){
    int tx=f->fields[i].x+f->fields[i].width-1;
    int ty=f->fields[i].y;
    if(tx<x && f->fields[i].active){
      double testdist=abs(ty-y)*100+abs(tx-x);
      if(testdist<dist){
	best=i;
	dist=testdist;
      }
    }
  }
  f->cursor=best;
  
  draw_field(f->fields+temp);
  draw_field(f->fields+f->cursor);
}

void form_right_field(form *f){
  int temp=f->cursor;
  int i;
  double dist=99999;
  int best=f->cursor;
  int x=f->fields[f->cursor].x+f->fields[f->cursor].width-1;
  int y=f->fields[f->cursor].y;
  for(i=0;i<f->count;i++){
    int tx=f->fields[i].x;
    int ty=f->fields[i].y;
    if(tx>x && f->fields[i].active){
      double testdist=abs(ty-y)*100+abs(tx-x);
      if(testdist<dist){
	best=i;
	dist=testdist;
      }
    }
  }  
  f->cursor=best;
  
  draw_field(f->fields+temp);
  draw_field(f->fields+f->cursor);
}

void form_down_field(form *f){
  int temp=f->cursor;
  int i;
  double dist=99999;
  int best=f->cursor;
  int x=f->fields[f->cursor].x;
  int y=f->fields[f->cursor].y;
  for(i=0;i<f->count;i++){
    int tx=f->fields[i].x;
    int ty=f->fields[i].y;
    if(ty>y && f->fields[i].active){
      double testdist=abs(ty-y)+abs(tx-x)*100;
      if(testdist<dist){
	best=i;
	dist=testdist;
      }
    }
  }
  f->cursor=best;
  
  draw_field(f->fields+temp);
  draw_field(f->fields+f->cursor);
}

void form_up_field(form *f){
  int temp=f->cursor;
  int i;
  double dist=99999;
  int best=f->cursor;
  int x=f->fields[f->cursor].x;
  int y=f->fields[f->cursor].y;
  for(i=0;i<f->count;i++){
    int tx=f->fields[i].x;
    int ty=f->fields[i].y;
    if(ty<y && f->fields[i].active){
      double testdist=abs(ty-y)+abs(tx-x)*100;
      if(testdist<dist){
	best=i;
	dist=testdist;
      }
    }
  }
  f->cursor=best;
  
  draw_field(f->fields+temp);
  draw_field(f->fields+f->cursor);
}

/* returns >=0 if it does not handle the character */
int form_handle_char(form *f,int c){
  formfield *ff=f->fields+f->cursor;
  int ret=-1;

  switch(c){
  case KEY_UP:
    form_up_field(f);
    break;
  case KEY_DOWN:
    form_down_field(f);
    break;
  case '\t':
    form_next_field(f);
    break;
  case KEY_BTAB:
    form_prev_field(f);
    break;
  case KEY_LEFT:
    if(ff->type==FORM_TIME && ff->cursor>0){
      ff->cursor--;
    }else{
      form_left_field(f);
    }
    break;
  case KEY_RIGHT:
    if(ff->type==FORM_TIME){
      ff->cursor++;
      if(ff->cursor>=ff->editwidth)ff->cursor=ff->editwidth-1;
    }else{
      form_right_field(f);
    }
    break;
  default:
    pthread_mutex_lock(&master_mutex);
    switch(ff->type){
      
    case FORM_DB:
      {
	long *val=(long *)ff->var;
	switch(c){
	case '=':
	  (*val)++;
	  if(*val>ff->max)*val=ff->max;
	  if(ff->cb)ff->cb();
	  break;
	case '+':
	  (*val)+=10;
	  if(*val>ff->max)*val=ff->max;
	  if(ff->cb)ff->cb();
	  break;
	case '-':
	  (*val)--;
	  if(*val<ff->min)*val=ff->min;
	  if(ff->cb)ff->cb();
	  break;
	case '_':
	  (*val)-=10;
	  if(*val<ff->min)*val=ff->min;
	  if(ff->cb)ff->cb();
	  break;
	default:
	  ret=c;
	  break;
	}
      }
      break;
    case FORM_P2:
      {
	long *val=(long *)ff->var;
	switch(c){
	case '=':case '+':
	  (*val)*=2;
	  if(*val>ff->max)*val=ff->max;
	  if(ff->cb)ff->cb();
	  break;
	case '-':case '_':
	  (*val)/=2;
	  if(*val<ff->min)*val=ff->min;
	  if(ff->cb)ff->cb();
	  break;
	default:
	  ret=c;
	  break;
	}
      }
      break;
    case FORM_TIME:
      {
	long *val=(long *)ff->var;
	switch(c){
	case '0':case '1':case '2':case '3':case '4':
	case '5':case '6':case '7':case '8':case '9':
	  {
	    long mult=pow10(ff->editwidth-ff->cursor-1);
	    long oldnum=(*val/mult)%10;
	    
	    *val-=oldnum*mult;

	    if(*val==-1)*val=0;
	    *val+=(c-48)*mult;
	    
	    ff->cursor++;
	    if(ff->cursor>=ff->editwidth)ff->cursor=ff->editwidth-1;
	    if(ff->cb)ff->cb();
	  }
	  break;
	case KEY_BACKSPACE:case '\b':
	  {
	    long mult=pow10(ff->editwidth-ff->cursor-1);
	    long oldnum=(*val/mult)%10;
	    
	    *val-=oldnum*mult;
	    ff->cursor--;
	    if(ff->cursor<0)ff->cursor=0;
	    if(ff->cb)ff->cb();
	  }
	  break;
	default:
	  ret=c;
	  break;
	}
      }
      break;
    default:
      ret=c;
      break;
    }
    pthread_mutex_unlock(&master_mutex);
  } 
  draw_field(f->fields+f->cursor);    
  return(ret);
}

