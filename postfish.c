/*
 *
 *  postfish.c
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

/* sound playback code is OSS-specific for now */

#define _GNU_SOURCE
#define _LARGEFILE_SOURCE 
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64
#define _REENTRANT 1
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#define __USE_GNU 1
#include <pthread.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <curses.h>
#include <fcntl.h>

#include <sys/soundcard.h>
#include <sys/ioctl.h>

#include <smallft.h>
#include <form.h>

#include "envelope.h"

#define todB(x)   ((x)==0?-400.f:log((x)*(x))*4.34294480f)
#define fromdB(x) (exp((x)*.11512925f))  
#define toOC(n)     (log(n)*1.442695f-5.965784f)

#define MAX_BLOCKSIZE 32768
#define BANDS 35

static int outfileno=-1;
static int loop_flag=1;
static int inbytes=0;
static int outbytes=2;
static int rate=0;
static int ch=0;
static int signp=0;
#define MAX_DECAY_MS 2000

/* saved, but not part of a config profile */
static long TX[10]={-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}; 
static long A=0;
static long B=-1;
static long eq_m=1;
static long n_m=1;

typedef struct {
  long block_a;
  
  long dyn_p;
  long dynms;
  long dynt;
  long masteratt;
  long masteratt_p;
  long dynamicatt;
  long dynamicatt_p;
  long eq_p;
  long eqt[BANDS];
  long noise_p;
  long noiset[BANDS];

  long used;

} configprofile;

#define CONFIG_MAX 5
static long configactive=0;
static configprofile configlist[CONFIG_MAX]={
  {
    8192,2,3000,-30,0,1,0,1,1,
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
     0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    1,
    {-1200,-1200,-1200,-1200,-1200,-1200,-1200,-1200,-1200,
     -1200,-1200,-1200,-1200,-1200,-1200,-1200,-1200,-1200,
     -1200,-1200,-1200,-1200,-1200,-1200,-1200,-1200,-1200,
     -1200,-1200,-1200,-1200,-1200,-1200,-1200,-1200},
    1,
  }
};
static configprofile configdefault={
  8192,2,3000,-30,0,1,0,1,1,
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  1,
    {-1200,-1200,-1200,-1200,-1200,-1200,-1200,-1200,-1200,
     -1200,-1200,-1200,-1200,-1200,-1200,-1200,-1200,-1200,
     -1200,-1200,-1200,-1200,-1200,-1200,-1200,-1200,-1200,
     -1200,-1200,-1200,-1200,-1200,-1200,-1200,-1200},
  0,
};
static configprofile wc;

/* working space */

static long block=8192;

static int feedback_n_displayed=0;
static int feedback_e_displayed=0;
static int feedback_a_displayed=0;
static int feedback_l_displayed=0;

static double eqt_feedbackmax[BANDS]={
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static double eqt_feedbackav[BANDS]={
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static long eqt_feedbackcount[BANDS]={
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

static int eq_dirty=1;
static double eq_tfull[(MAX_BLOCKSIZE>>1)+1];

static double noiset_feedbackmax[BANDS]={
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static double noiset_feedbackav[BANDS]={
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

static long noiset_feedbackcount[BANDS]={
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

static int noise_dirty=1; /* mark on blocksize or noise filter setting changes */
static double noise_tfull[(MAX_BLOCKSIZE>>1)+1];
static int oc[(MAX_BLOCKSIZE>>1)+1];

static double *dyn_decay=NULL;
static long dyn_decaysize=0;

static double maxtimepre=0;
static double maxtimepost=0;
static double maxtimeprehold=0;
static double maxtimeposthold=0;

static off_t Acursor=0;
static off_t Bcursor=-1;
static long  T=-1;
static off_t cursor=0;
static long  primed=0;

pthread_mutex_t master_mutex=PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

int playback_active=0;
int playback_exit=0;
drft_lookup fft;
env_lookup envpre,envpost;
pthread_t playback_thread_id;


int ttyfd;
int ttypipe[2];
pthread_t tty_thread_id;

typedef struct {
  FILE *f;
  
  off_t begin;
  off_t end;
  off_t data;

} file_entry;

file_entry *file_list=NULL;
int file_entries=0;
int current_file_entry_number=-1;
file_entry *current_file_entry=NULL;
int seekable=0;

/* -------------------------------------- */

#define N_Y 2
#define N_X 2

#define E_Y 2
#define E_X 36

#define D_Y 2
#define D_X 70

#define T_Y (7+BANDS)
#define T_X 6

#define C_Y (BANDS-CONFIG_MAX+3)
#define C_X D_X

form editf;
form noneditf;


void update_N(){
  mvaddstr(N_Y,N_X+4,"     [n] Noisegate ");
  if(wc.noise_p){
    attron(A_BOLD);
    addstr("ON ");
  }else{
    attron(A_BOLD);
    addstr("OFF");
  }

  attrset(0);

  mvaddstr(N_Y+1,N_X+4,"     [N] Edit Band ");
  if(n_m){
    attron(A_BOLD);
    addstr("NARROW");
  }else{
    attron(A_BOLD);
    addstr("WIDE  ");
  }

  attrset(0);
}

void dirty_the_noise(){
  int i,j;
  noise_dirty=1;

  /* really evil little hack to implement 'wideband' editing */
  /* compare working settings to the copy stored in profile to figure out
     what just got incremented/decremented */
  if(!n_m){
    for(i=0;i<BANDS;i++)
      if(wc.noiset[i]!=configlist[configactive].noiset[i])break;
    if(i<BANDS){
      int del=wc.noiset[i]-configlist[configactive].noiset[i];
      for(j=-3;j<4;j++){
	if(i+j>=0 && i+j<BANDS){
	  switch(j){
	  case -3:case 3:
	    wc.noiset[i+j]+=del*.2;
	    break;
	  case -2:case 2:
	    wc.noiset[i+j]+=del*.5;
	    break;
	  case -1:case 1:
	    wc.noiset[i+j]+=del*.8;
	    break;
	  }
	}
      }
    }
    memcpy(configlist[configactive].noiset,wc.noiset,sizeof(wc.noiset));
    form_redraw(&editf);
  }
}

void dirty_the_eq(){
  int i,j;
  eq_dirty=1;

  /* really evil little hack to implement 'wideband' editing */
  /* compare working settings to the copy stored in profile to figure out
     what just got incremented/decremented */
  if(!eq_m){
    for(i=0;i<BANDS;i++)
      if(wc.eqt[i]!=configlist[configactive].eqt[i])break;
    if(i<BANDS){
      int del=wc.eqt[i]-configlist[configactive].eqt[i];
      for(j=-3;j<4;j++){
	if(i+j>=0 && i+j<BANDS){
	  switch(j){
	  case -3:case 3:
	    wc.eqt[i+j]+=del*.2;
	    break;
	  case -2:case 2:
	    wc.eqt[i+j]+=del*.5;
	    break;
	  case -1:case 1:
	    wc.eqt[i+j]+=del*.8;
	    break;
	  }
	}
      }
    }
    memcpy(configlist[configactive].eqt,wc.eqt,sizeof(wc.noiset));
    form_redraw(&editf);
  }
}

void update_static_N(){
  int i;
  mvaddstr(N_Y+3, N_X,"  63 ");
  mvaddstr(N_Y+5, N_X,"  88 ");
  mvaddstr(N_Y+7, N_X," 125 ");
  mvaddstr(N_Y+9, N_X," 175 ");
  mvaddstr(N_Y+11,N_X," 250 ");
  mvaddstr(N_Y+13,N_X," 350 ");
  mvaddstr(N_Y+15,N_X," 500 ");
  mvaddstr(N_Y+17,N_X," 700 ");
  mvaddstr(N_Y+19,N_X,"  1k ");
  mvaddstr(N_Y+21,N_X,"1.4k ");
  mvaddstr(N_Y+23,N_X,"  2k ");
  mvaddstr(N_Y+25,N_X,"2.8k ");
  mvaddstr(N_Y+27,N_X,"  4k ");
  mvaddstr(N_Y+29,N_X,"5.6k ");
  mvaddstr(N_Y+31,N_X,"  8k ");
  mvaddstr(N_Y+33,N_X," 11k ");
  mvaddstr(N_Y+35,N_X," 16k ");
  mvaddstr(N_Y+37,N_X," 22k ");

  mvvline(N_Y+3,N_X+9,0,BANDS);
  mvvline(N_Y+3,N_X+20,0,BANDS);
  mvvline(N_Y+3,N_X+31,0,BANDS);

  mvhline(N_Y+2,N_X+9,0,23);
  mvhline(N_Y+3+BANDS,N_X+9,0,23);

  mvaddch(N_Y+2,N_X+9,ACS_ULCORNER);
  mvaddch(N_Y+2,N_X+31,ACS_URCORNER);
  mvaddch(N_Y+2,N_X+20,ACS_TTEE);
  mvaddch(N_Y+3+BANDS,N_X+9,ACS_LLCORNER);
  mvaddch(N_Y+3+BANDS,N_X+31,ACS_LRCORNER);

  mvaddstr(N_Y+3+BANDS,N_X,"       dB");
  mvaddstr(N_Y+3+BANDS,N_X+10,"30- ");
  mvaddstr(N_Y+3+BANDS,N_X+19," 0 ");
  mvaddstr(N_Y+3+BANDS,N_X+27," +30");

  for(i=0;i<BANDS;i++)
    field_add(&editf,FORM_DB,N_X+5,N_Y+3+i,4,&wc.noiset[i],
	      dirty_the_noise,0,-1500,0);
}

void update_E(){
  mvaddstr(E_Y,E_X+4,"    [e] Equalizer ");
  if(wc.eq_p){
    attron(A_BOLD);
    addstr("ON ");
  }else{
    attron(A_BOLD);
    addstr("OFF");
  }
  attrset(0);
  mvaddstr(E_Y+1,E_X+4,"    [E] Edit Band ");
  if(eq_m){
    attron(A_BOLD);
    addstr("NARROW");
  }else{
    attron(A_BOLD);
    addstr("WIDE  ");
  }
  attrset(0);
}

void update_static_E(){
  int i;
  mvaddstr(E_Y+3, E_X,"  63 ");
  mvaddstr(E_Y+5, E_X,"  88 ");
  mvaddstr(E_Y+7, E_X," 125 ");
  mvaddstr(E_Y+9, E_X," 175 ");
  mvaddstr(E_Y+11,E_X," 250 ");
  mvaddstr(E_Y+13,E_X," 350 ");
  mvaddstr(E_Y+15,E_X," 500 ");
  mvaddstr(E_Y+17,E_X," 700 ");
  mvaddstr(E_Y+19,E_X,"  1k ");
  mvaddstr(E_Y+21,E_X,"1.4k ");
  mvaddstr(E_Y+23,E_X,"  2k ");
  mvaddstr(E_Y+25,E_X,"2.8k ");
  mvaddstr(E_Y+27,E_X,"  4k ");
  mvaddstr(E_Y+29,E_X,"5.6k ");
  mvaddstr(E_Y+31,E_X,"  8k ");
  mvaddstr(E_Y+33,E_X," 11k ");
  mvaddstr(E_Y+35,E_X," 16k ");
  mvaddstr(E_Y+37,E_X," 22k ");

  mvvline(E_Y+3,E_X+8,0,BANDS);
  mvvline(E_Y+3,E_X+30,0,BANDS);

  mvhline(E_Y+2,E_X+8,0,23);
  mvhline(E_Y+3+BANDS,E_X+8,0,23);

  mvaddch(E_Y+2,E_X+8,ACS_ULCORNER);
  mvaddch(E_Y+2,E_X+30,ACS_URCORNER);
  mvaddch(E_Y+3+BANDS,E_X+8,ACS_LLCORNER);
  mvaddch(E_Y+3+BANDS,E_X+30,ACS_LRCORNER);

  mvaddstr(E_Y+3+BANDS,E_X,"      dB");
  mvaddstr(E_Y+3+BANDS,E_X+9,"30- ");
  mvaddstr(E_Y+3+BANDS,E_X+18," 0 ");
  mvaddstr(E_Y+3+BANDS,E_X+26," +30");

  mvaddstr(E_Y+2,E_X+9,"120- ");
  mvaddstr(E_Y+2,E_X+27," +0");

  for(i=0;i<BANDS;i++)
    field_add(&editf,FORM_DB,E_X+5,E_Y+3+i,3,&wc.eqt[i],
	      dirty_the_eq,0,-300,300);
}


static formfield *A_field;
static formfield *B_field;
static formfield *T_field;

static formfield *pre_field;
static formfield *post_field;
static formfield *limit_field;
static long pre_var=0;
static long post_var=0;

off_t time_to_cursor(long t){
  if(t<0)
    return(-1);

  {
    off_t c=t%10000;
    
    c+=t/10000%100*6000;
    c+=t/1000000*6000*60;
    
    return((off_t)rint(c*.01*rate)*ch*inbytes);
  }
}

void dirty_A(){
  Acursor=time_to_cursor(A);
}
void dirty_B(){
  Bcursor=time_to_cursor(B);
}

long cursor_to_time(off_t c){
  long T;
  if(c<0)return(-1);
  c=c*100./rate/ch/inbytes;
  T =c/(100*60*60)*1000000;
  c%=(100*60*60);
  T+=c/(100*60)*10000;
  c%= (100*60);
  T+=c/(100)*100;
  T+=c%100;
  return(T);
}


void update_D(){
  attron(A_BOLD);

  move(D_Y,D_X+18);
  if(wc.masteratt_p)
    addstr("ON ");
  else
    addstr("OFF");

  move(D_Y+1,D_X+18);
  if(wc.dynamicatt_p)
    addstr("ON ");
  else
    addstr("OFF");
  
  move(D_Y+8,D_X+12);
  switch(wc.dyn_p){
  case 0:
    addstr("      OFF");
    field_active(limit_field,1);
    break;
  case 1:
    addstr("BLOCK ATT");
    field_active(limit_field,1);
    break;
  case 2:
    addstr("SOFT CLIP");
    field_active(limit_field,0);
    break;
  }
  attrset(0);
}

void update_static_D(){

  mvaddstr(D_Y+0,D_X,"[m]    Master Att            dB");
  mvaddstr(D_Y+1,D_X,"[r] Dynamic Range            dB");
  mvaddstr(D_Y+2,D_X,"       Frame size");

  mvaddstr(D_Y+5,D_X,"                             dB");

  mvvline(D_Y+5,D_X,0,1);
  mvvline(D_Y+5,D_X+11,0,1);
  mvvline(D_Y+5,D_X+22,0,1);

  mvhline(D_Y+4,D_X,0,23);
  mvhline(D_Y+6,D_X,0,23);

  mvaddch(D_Y+4,D_X,ACS_ULCORNER);
  mvaddch(D_Y+4,D_X+22,ACS_URCORNER);
  mvaddch(D_Y+4,D_X+11,ACS_TTEE);
  mvaddch(D_Y+6,D_X,ACS_LLCORNER);
  mvaddch(D_Y+6,D_X+22,ACS_LRCORNER);

  mvaddstr(D_Y+6,D_X+1,"30- ");
  mvaddstr(D_Y+6,D_X+10," 0 ");
  mvaddstr(D_Y+6,D_X+18," +30");

  mvaddstr(D_Y+8,D_X,"[l] Limiter                  dB");
  mvaddstr(D_Y+9,D_X,"    Block Period             ms");
  mvaddstr(D_Y+11,D_X,"                             dB");

  mvvline(D_Y+11,D_X,0,1);
  mvvline(D_Y+11,D_X+11,0,1);
  mvvline(D_Y+11,D_X+22,0,1);

  mvhline(D_Y+10,D_X,0,23);
  mvhline(D_Y+12,D_X,0,23);

  mvaddch(D_Y+10,D_X,ACS_ULCORNER);
  mvaddch(D_Y+10,D_X+22,ACS_URCORNER);
  mvaddch(D_Y+10,D_X+11,ACS_TTEE);
  mvaddch(D_Y+12,D_X,ACS_LLCORNER);
  mvaddch(D_Y+12,D_X+22,ACS_LRCORNER);

  mvaddstr(D_Y+12,D_X+1,"30- ");
  mvaddstr(D_Y+12,D_X+10," A ");
  mvaddstr(D_Y+12,D_X+19," +0");

  field_add(&editf,FORM_DB,D_X+23,D_Y+0,5,&wc.masteratt,NULL,1,-900,+900);
  field_add(&editf,FORM_DB,D_X+23,D_Y+1,5,&wc.dynamicatt,NULL,1,-900,+900);
  field_add(&editf,FORM_P2,D_X+23,D_Y+2,5,&wc.block_a,NULL,0,64,MAX_BLOCKSIZE);
  field_add(&editf,FORM_DB,D_X+23,D_Y+8,5,&wc.dynt,NULL,1,-300,0);
  limit_field=field_add(&editf,FORM_DB,D_X+23,D_Y+9,5,&wc.dynms,NULL,0,0,MAX_DECAY_MS*10);

  mvaddstr(D_Y+14,D_X,"[a]             [A] Clear");
  mvaddstr(D_Y+15,D_X,"[b]             [B] Clear");

  A_field=field_add(&editf,FORM_TIME,D_X+4,D_Y+14,11,&A,dirty_A,0,0,99999999);
  B_field=field_add(&editf,FORM_TIME,D_X+4,D_Y+15,11,&B,dirty_B,0,0,99999999);
  if(!seekable){
    field_active(A_field,0);
    field_active(B_field,0);
  }
  noneditf.cursor=-1;

  T_field=field_add(&noneditf,FORM_TIME,D_X+4,D_Y+16,11,&T,NULL,0,0,99999999);

  pre_field=field_add(&noneditf,FORM_DB,D_X+23,D_Y+5,5,&pre_var,NULL,0,-300,300);
  post_field=field_add(&noneditf,FORM_DB,D_X+23,D_Y+11,5,&post_var,NULL,0,-300,00);

}

void update_0(){
  /* redraw the whole form; easiest */
  form_redraw(&noneditf);

}


void update_C(){
  mvaddstr(C_Y,C_X,"Configuration cache:");
  
  if(configactive==0)
    mvaddstr(C_Y+2,C_X,"<v>>CURRENT [V] Clear");
  else
    if(configlist[0].used)
      mvaddstr(C_Y+2,C_X,"[v] full    [V] Clear");
    else
      mvaddstr(C_Y+2,C_X,"[v]         [V] Clear");

  if(configactive==1)
    mvaddstr(C_Y+3,C_X,"<w>>CURRENT [W] Clear");
  else
    if(configlist[1].used)
      mvaddstr(C_Y+3,C_X,"[w] full    [W] Clear");
    else
      mvaddstr(C_Y+3,C_X,"[w]         [W] Clear");

  if(configactive==2)
    mvaddstr(C_Y+4,C_X,"<x>>CURRENT [X] Clear");
  else
    if(configlist[2].used)
      mvaddstr(C_Y+4,C_X,"[x] full    [X] Clear");
    else
      mvaddstr(C_Y+4,C_X,"[x]         [X] Clear");

  if(configactive==3)
    mvaddstr(C_Y+5,C_X,"<y>>CURRENT [Y] Clear");
  else
    if(configlist[3].used)
      mvaddstr(C_Y+5,C_X,"[y] full    [Y] Clear");
    else
      mvaddstr(C_Y+5,C_X,"[y]         [Y] Clear");

  if(configactive==4)
    mvaddstr(C_Y+6,C_X,"<z>>CURRENT [Z] Clear");
  else
    if(configlist[4].used)
      mvaddstr(C_Y+6,C_X,"[z] full    [Z] Clear");
    else
      mvaddstr(C_Y+6,C_X,"[z]         [Z] Clear");

}

void update_static_0(){

  mvaddstr(T_Y,T_X,"[0>");
  field_add(&noneditf,FORM_TIME,T_X+3,T_Y,11,&TX[0],NULL,0,0,99999999);
  mvaddstr(T_Y,T_X+19,"[1>");
  field_add(&noneditf,FORM_TIME,T_X+22,T_Y,11,&TX[1],NULL,0,0,99999999);
  mvaddstr(T_Y,T_X+38,"[2>");
  field_add(&noneditf,FORM_TIME,T_X+41,T_Y,11,&TX[2],NULL,0,0,99999999);
  mvaddstr(T_Y,T_X+57,"[3>");
  field_add(&noneditf,FORM_TIME,T_X+60,T_Y,11,&TX[3],NULL,0,0,99999999);
  mvaddstr(T_Y,T_X+76,"[4>");
  field_add(&noneditf,FORM_TIME,T_X+79,T_Y,11,&TX[4],NULL,0,0,99999999);

  mvaddstr(T_Y+1,T_X,"[5>");
  field_add(&noneditf,FORM_TIME,T_X+3,T_Y+1,11,&TX[5],NULL,0,0,99999999);
  mvaddstr(T_Y+1,T_X+19,"[6>");
  field_add(&noneditf,FORM_TIME,T_X+22,T_Y+1,11,&TX[6],NULL,0,0,99999999);
  mvaddstr(T_Y+1,T_X+38,"[7>");
  field_add(&noneditf,FORM_TIME,T_X+41,T_Y+1,11,&TX[7],NULL,0,0,99999999);
  mvaddstr(T_Y+1,T_X+57,"[8>");
  field_add(&noneditf,FORM_TIME,T_X+60,T_Y+1,11,&TX[8],NULL,0,0,99999999);
  mvaddstr(T_Y+1,T_X+76,"[9>");
  field_add(&noneditf,FORM_TIME,T_X+79,T_Y+1,11,&TX[9],NULL,0,0,99999999);

}

void update_a(){
  draw_field(A_field);
}

void update_b(){
  draw_field(B_field);
}

void update_ui(int up){
  int i,j;
  int valM[BANDS],valA[BANDS];
  
  pthread_mutex_lock(&master_mutex); 
  T=cursor_to_time(cursor);
  pthread_mutex_unlock(&master_mutex); 
  draw_field(T_field);
  
  pthread_mutex_lock(&master_mutex);
  for(i=0;i<BANDS;i++){
    valM[i]=rint(todB(eqt_feedbackmax[i])/5.7142857+21);
    valA[i]=rint(todB(eqt_feedbackav[i]/eqt_feedbackcount[i])/5.7142857+21);
  }
  if(up)feedback_e_displayed=1;
  pthread_mutex_unlock(&master_mutex);

  for(i=0;i<BANDS;i++){
    int valT=rint(wc.eqt[i]/30.+10);
    
    move(E_Y+3+i,E_X+9);
    for(j=0;j<valA[i] && j<21;j++){
      if(j==valT)
	addch(ACS_VLINE);
      else
	addch(' ');
    }

    for(;j<=valM[i] && j<21;j++){
      if(j==valT)
	addch(ACS_PLUS);
      else
	addch(ACS_HLINE);
    }
    
    for(;j<21;j++){
      if(j==valT)
	addch(ACS_VLINE);
      else
	addch(' ');
    }
  }
}

void update_play(int up){
  int i,j;
  int valM[BANDS],valA[BANDS];

  pthread_mutex_lock(&master_mutex); 
  update_ui(up);
  if(playback_active){
    if(B!=-1){
      pthread_mutex_unlock(&master_mutex);
      mvaddstr(D_Y+16,D_X,"A-B");
    }else{
      pthread_mutex_unlock(&master_mutex);
      mvaddstr(D_Y+16,D_X,">>>");
    }
  }else{
    pthread_mutex_unlock(&master_mutex);
    if(T==0)
      mvaddstr(D_Y+16,D_X,"   ");
    else
      if(T==A)
	mvaddstr(D_Y+16,D_X,"CUE");
      else
	mvaddstr(D_Y+16,D_X,"|||");
  }
  
  pthread_mutex_lock(&master_mutex);
  for(i=0;i<BANDS;i++){
    if(noiset_feedbackcount[i]){
      valM[i]=rint(todB(noiset_feedbackmax[i])/3.+10);
      valA[i]=rint(todB(noiset_feedbackav[i]/noiset_feedbackcount[i])/3.+10);
    }else{
      valM[i]=-1;
      valA[i]=-1;
    }
  }
  if(up)feedback_n_displayed=1;
  pthread_mutex_unlock(&master_mutex);

  for(i=0;i<BANDS;i++){
    move(N_Y+3+i,N_X+10);
    for(j=0;j<valA[i] && j<=21;j++){
      if(j==21)
	addch('+');
      else
	if(j==10)
	  addch(ACS_VLINE);
	else
	  addch(' ');
    }

    for(;j<=valM[i] && j<=21;j++){
      if(j==21)
	addch('+');
      else
	if(j==10)
	  addch(ACS_PLUS);
	else
	  addch(ACS_HLINE);
    }


    for(;j<22;j++){
      if(j==21 || j==10)
	addch(ACS_VLINE);
      else
	addch(' ');
    }

  }

  /* pre-limit bargraph */
  {
    double val;
    int ival;
    move(D_Y+5,D_X+1);
    pthread_mutex_lock(&master_mutex);
    if(maxtimeprehold){
      val=maxtimepre;
      pre_var=rint(todB(maxtimeprehold)*10.);
      if(up)feedback_a_displayed=1;
      pthread_mutex_unlock(&master_mutex);
      
      ival=(todB(val)+30.)/3.;
      if(ival<0)ival=0;
      if(ival>22)ival=22;
      
      for(i=0;i<ival;i++){
	if(i==10){
	  addch(ACS_PLUS);
	}else if(i==21){
	  addch('+');
	}else
	  addch(ACS_HLINE);
      }
      for(;i<22;i++){
	if(i==10){
	  addch(ACS_VLINE);
	}else if(i==21){
	  addch(ACS_VLINE);
	}else
	  addch(' ');
      }
      
      draw_field(pre_field);
    }else{
      pthread_mutex_unlock(&master_mutex);
      for(i=0;i<22;i++){
	if(i==10 || i==21){
	  addch(ACS_VLINE);
	}else
	  addch(' ');
      }
    }
  }

  /* post-limit bargraph */
  {
    double val;
    int ival;
    move(D_Y+11,D_X+1);
    pthread_mutex_lock(&master_mutex);
    if(&wc.dyn_p && maxtimeposthold){
      val=maxtimepost;
      post_var=rint(todB(maxtimeposthold)*10.);
      if(up)feedback_l_displayed=1;
      pthread_mutex_unlock(&master_mutex);
      
      if(todB(val)<wc.dynt*.1){
	ival=(todB(val)+30.)/(wc.dynt*.1+30)*10;
	if(ival<0)ival=0;
      }else{
	if(wc.dynt==0){
	  ival=22;	   
	}else{
	  ival=(todB(val)-wc.dynt*.1)/(-wc.dynt*.1)*10+10;
	  if(ival>22)ival=22;
	}
      }
      
      for(i=0;i<ival;i++){
	if(i==10){
	  addch(ACS_PLUS);
	}else if(i==21){
	  addch('+');
	}else
	  addch(ACS_HLINE);
      }
      for(;i<22;i++){
	if(i==10){
	  addch(ACS_VLINE);
	}else if(i==21){
	  addch(ACS_VLINE);
	}else
	  addch(' ');
      }
      
      draw_field(post_field);
    }else{
      pthread_mutex_unlock(&master_mutex);
      for(i=0;i<22;i++){
	if(i==10 || i==21){
	  addch(ACS_VLINE);
	}else
	  addch(' ');
      }
    }
  }
    
}


/* simple dB-scale volume scaling */
void master_att(double *b){
  int i;
  pthread_mutex_lock(&master_mutex);
  if(wc.masteratt_p && wc.masteratt!=0.){
    double val=fromdB(wc.masteratt*.1);
    pthread_mutex_unlock(&master_mutex);

    for(i=0;i<block;i++)
      b[i]*=val;

  }else
    pthread_mutex_unlock(&master_mutex);
}  

inline double inv_amplitude(double amplitude,double thresh){
  if(fabs(amplitude)>thresh){
    double scale=(1.-thresh);
    if(amplitude>0)
      return ( (amplitude-thresh)/(scale+(amplitude-thresh)) )*scale + thresh ;
    else
      return ( (thresh+amplitude)/(scale-(thresh+amplitude)) )*scale - thresh ;
  }else
    return(amplitude);
}

inline double inv_scale(double amplitude,double thresh){
  if(amplitude==0.)return 1.;
  return(inv_amplitude(amplitude,thresh)/amplitude);
}

/* limit output by block; atan-scale according to max amplitude */
void dynamic_limit(double **b){
  int i,j;
  pthread_mutex_lock(&master_mutex);
  {
    double thresh=fromdB(wc.dynt*.1);
    double y=0;
    double d=0;
    long ms=wc.dynms/10;

    if(feedback_a_displayed)
      maxtimepre/=2;
    if(feedback_l_displayed)
      maxtimepost/=2;
    feedback_a_displayed=0;
    feedback_l_displayed=0;

    pthread_mutex_unlock(&master_mutex);
    
    for(j=0;j<ch;j++)
      for(i=0;i<block;i++)
	if(fabs(b[j][i])>y)y=fabs(b[j][i]);

    memmove(dyn_decay+1,dyn_decay,sizeof(*dyn_decay)*(dyn_decaysize-1));
    dyn_decay[0]=y;
    j=rint(rate*.002*ms/block);

    if(j>dyn_decaysize)j=dyn_decaysize; /* be certain */
    for(i=1;i<j;i++)
      if(y<dyn_decay[i])y=dyn_decay[i];

    d=inv_scale(y,thresh);
    y=dyn_decay[0];

    pthread_mutex_lock(&master_mutex);

    /* valid for either block att or soft clip */
    if(maxtimepre<y)maxtimepre=y;
    if(maxtimepost<y*d)maxtimepost=y*d;
    if(maxtimeprehold<y)maxtimeprehold=y;
    if(maxtimeposthold<y*d)maxtimeposthold=y*d;

    switch(wc.dyn_p){
    case 2:
      d=thresh;
      pthread_mutex_unlock(&master_mutex);
      
      for(j=0;j<ch;j++)
	for(i=0;i<block;i++)
	  b[j][i]=inv_amplitude(b[j][i],d);
      break;
    case 1:
      if(d!=1.){
	pthread_mutex_unlock(&master_mutex);
	
	for(j=0;j<ch;j++)
	  for(i=0;i<block;i++)
	    b[j][i]*=d;
	break;
      }
      /* partial fall-through */
    default:
      pthread_mutex_unlock(&master_mutex);
    }
  }
}  

/* stretch/shrink dynamic range of output.  argument is dB added to a
   nominal 120dB range */
void dynamic_att(double *b){
  int i;
  pthread_mutex_lock(&master_mutex);
  if(wc.dynamicatt_p && wc.dynamicatt!=0.){
    double dBper120=wc.dynamicatt/1200.;
    pthread_mutex_unlock(&master_mutex);

    for(i=0;i<block;i++)
      b[i]*= fromdB(todB(b[i])*dBper120);

  }else
    pthread_mutex_unlock(&master_mutex);
}  

void dynamic_sq(double *b){
  int i;
  pthread_mutex_lock(&master_mutex);
  if(wc.dynamicatt_p && wc.dynamicatt!=0.){
    double diff=fromdB(wc.dynamicatt/10.);
    pthread_mutex_unlock(&master_mutex);
    
    env_compute(&envpre,b);
    
    /* zero */
    {
      double val=diff*envpre.envelope[0];
      double M=b[0];
      if(M-val>0){	
	double div=(M-val)/M;
	b[0]*=div;
      }else{
	b[0]=0;
      }
    }
      
    /* 1 ... n-1 */
    for(i=1;i+1<block-1;i+=2){
      double val=diff*envpre.envelope[(i>>1)+1];
      double M=hypot(b[i],b[i+1]);
      if(M-val>0){	
	double div=(M-val)/M;
	b[i]*=div;
	b[i+1]*=div;
      }else{
	b[i]=0;
	b[i+1]=0;
      }
    }

    /* n */
    {
      double val=diff*envpre.envelope[block/2-1];
      double M=b[block/2-1];
      if(M-val>0){	
	double div=(M-val)/M;
	b[block/2-1]*=div;
      }else{
	b[block/2-1]=0;
      }
    }

  }else
    pthread_mutex_unlock(&master_mutex);
}  

void refresh_thresh_array(double *t,long *set){
  int i;
  for(i=0;i<=block/2;i++){
    double hoc=toOC(i/(block*.5)*22050.)*4.;
    int ihoc=hoc;
    double dhoc=hoc-ihoc;
    double dB;

    if(ihoc>=BANDS-1){
      hoc=BANDS-1;
      ihoc=BANDS-2;
      dhoc=1.;
    }
    if(hoc<0.){
      hoc=0;
      ihoc=0;
      dhoc=0.;
    }

    oc[i]=rint(hoc);

    dhoc=sin(dhoc*M_PI/2)*sin(dhoc*M_PI/2);
    dB=(set[ihoc]/10)*(1.-dhoc)+(set[ihoc+1]/10)*dhoc;

    t[i]=fromdB(dB);
  }

}

void noise_filter(double *b){
  int i;
  double dv;
  pthread_mutex_lock(&master_mutex);

  if(noise_dirty){
    refresh_thresh_array(noise_tfull,wc.noiset);
    noise_dirty=0;
  }
  
  if(feedback_n_displayed)
    for(i=0;i<BANDS;i++){
      noiset_feedbackav[i]/=2;
      noiset_feedbackmax[i]/=2;
      noiset_feedbackcount[i]/=2;
    }
  feedback_n_displayed=0;

  pthread_mutex_unlock(&master_mutex);
  
  dv=fabs(b[0])/noise_tfull[0];
  if(noiset_feedbackmax[oc[0]]<dv)noiset_feedbackmax[oc[0]]=dv;
  noiset_feedbackav[oc[0]]+=dv;
  noiset_feedbackcount[oc[0]]++;
  

  if(wc.noise_p){
    if(b[0]<0){
      b[0]+=noise_tfull[0];
      if(b[0]>0)b[0]=0;
    }else{
      b[0]-=noise_tfull[0];
      if(b[0]<0)b[0]=0;
    }
  }

  for(i=1;i<block/2;i++){
    double M=sqrt(b[i*2]*b[i*2]+b[i*2-1]*b[i*2-1]);
    
    dv=M/noise_tfull[i];
    if(noiset_feedbackmax[oc[i]]<dv)noiset_feedbackmax[oc[i]]=dv;
    noiset_feedbackav[oc[i]]+=dv;
    noiset_feedbackcount[oc[i]]++;
    
    if(wc.noise_p){
      if(M-noise_tfull[i]>0){
	double div=(M-noise_tfull[i])/M;
	b[i*2]*=div;
	b[i*2-1]*=div;
      }else{
	b[i*2]=0;
	b[i*2-1]=0;
      }
    }
  }
  
  dv=fabs(b[block-1])/noise_tfull[block/2];
  if(noiset_feedbackmax[oc[block/2]]<dv)noiset_feedbackmax[oc[block/2]]=dv;
  noiset_feedbackav[oc[block/2]]+=dv;
  noiset_feedbackcount[oc[block/2]]++;
  
  if(wc.noise_p){
    if(b[i*2-1]<0){
      b[i*2-1]+=noise_tfull[block/2];
      if(b[i*2-1]>0)b[i*2-1]=0;
    }else{
      b[i*2-1]-=noise_tfull[block/2];
      if(b[i*2-1]<0)b[i*2-1]=0;
    }
  }
}

void eq_filter(double *b){
  int i;
  double dv;
  pthread_mutex_lock(&master_mutex);

  if(eq_dirty){
    refresh_thresh_array(eq_tfull,wc.eqt);
    eq_dirty=0;
  }
      
  if(feedback_e_displayed)
    for(i=0;i<BANDS;i++){
      eqt_feedbackav[i]/=2;
      eqt_feedbackmax[i]/=2;
      eqt_feedbackcount[i]/=2;
    }
  feedback_e_displayed=0;
  pthread_mutex_unlock(&master_mutex);
  
  if(wc.eq_p)
    b[0]*=eq_tfull[0];
  dv=fabs(b[0]);
  if(eqt_feedbackmax[oc[0]]<dv)eqt_feedbackmax[oc[0]]=dv;
  eqt_feedbackav[oc[0]]+=dv;
  eqt_feedbackcount[oc[0]]++;
  
  for(i=1;i<block/2;i++){
    if(wc.eq_p){
      b[i*2]*=eq_tfull[i];
      b[i*2-1]*=eq_tfull[i];
    }
    
    dv=sqrt(b[i*2]*b[i*2]+b[i*2-1]*b[i*2-1]);
    if(eqt_feedbackmax[oc[i]]<dv)eqt_feedbackmax[oc[i]]=dv;
    eqt_feedbackav[oc[i]]+=dv;
    eqt_feedbackcount[oc[i]]++;
    
  }
  
  if(wc.eq_p)
    b[block-1]*=eq_tfull[block/2-1];
  dv=fabs(b[block-1]);
  if(eqt_feedbackmax[oc[i]]<dv)eqt_feedbackmax[oc[i]]=dv;
  eqt_feedbackav[oc[i]]+=dv;
  eqt_feedbackcount[oc[i]]++;
 
}

int aseek(off_t pos){
  int i;

  if(pos<0)pos=0;
  if(!seekable){
    current_file_entry=file_list;
    current_file_entry_number=0;
    return -1;
  }

  pthread_mutex_lock(&master_mutex);
  for(i=0;i<file_entries;i++){
    current_file_entry=file_list+i;
    current_file_entry_number=i;
    if(current_file_entry->begin<=pos && current_file_entry->end>pos){
      fseeko(current_file_entry->f,
	     pos-current_file_entry->begin+current_file_entry->data,
	     SEEK_SET);
      cursor=pos;
      pthread_mutex_unlock(&master_mutex);
      return 0;
    }
  }
  i--;

  pos=current_file_entry->end;
  fseeko(current_file_entry->f,
	 pos-current_file_entry->begin+current_file_entry->data,
	 SEEK_SET);
  cursor=pos;
  pthread_mutex_unlock(&master_mutex);
  return(0);
}

/* shift data if necessary, read across input file segment boundaries if necessary */
int aread(double **buf){
  int read_b=0,i,j,k;
  int toread_b=block*ch*inbytes;
  unsigned char *readbuf;
  double M,S;
  
  pthread_mutex_lock(&master_mutex);

  /* the non-streaming case */
  if(Bcursor<0 && 
     cursor>=current_file_entry->end &&
     current_file_entry->end!=-1){
    pthread_mutex_unlock(&master_mutex);
    return -1; /* EOF */
  }

  /* the streaming case */
  if(feof(current_file_entry->f) && 
     current_file_entry_number+1>=file_entries){
    pthread_mutex_unlock(&master_mutex);
    return -1;
  }

  if(primed){
    for(j=0;j<ch;j++)
      memmove(buf[j],buf[j]+block/2,sizeof(**buf)*block/2);
    toread_b/=2;
  }

  readbuf=alloca(toread_b);

  while(toread_b){
    off_t ret;
    off_t read_this_loop=current_file_entry->end-cursor;
    if(read_this_loop>toread_b)read_this_loop=toread_b;

    ret=fread(readbuf+read_b,1,read_this_loop,current_file_entry->f);

    if(ret>0){
      read_b+=ret;
      toread_b-=ret;
      cursor+=ret;
    }else{
      if(current_file_entry_number+1>=file_entries){
	memset(readbuf+read_b,0,toread_b);
	read_b+=toread_b;
	toread_b=0;
      }
    }

    if(Bcursor!=-1 && cursor>=Bcursor){
      if(loop_flag)
	aseek(Acursor);
      else{
	pthread_mutex_unlock(&master_mutex);
	return(-1);
      }
    }else if(cursor>=current_file_entry->end){
      if(current_file_entry_number+1<file_entries){
	current_file_entry_number++;
	current_file_entry++;
	fseeko(current_file_entry->f,current_file_entry->data,SEEK_SET);
      }
    }
  }
  
  k=0;
  for(i=(primed?block/2:0);i<block;i++){
    for(j=0;j<ch;j++){
      int val=0;
      switch(inbytes){
      case 1:
	val=(readbuf[k]<<24);
	break;
      case 2:
	val=(readbuf[k]<<16)|(readbuf[k+1]<<24);
	break;
      case 3:
	val=(readbuf[k]<<8)|(readbuf[k+1]<<16)|(readbuf[k+2]<<24);
	break;
      case 4:
	val=(readbuf[k])|(readbuf[k+1]<<8)|(readbuf[k+2]<<16)|(readbuf[k+3]<<24);
	break;
      }
      if(signp)
	buf[j][i]=val*4.6566128730e-10;
      else
	buf[j][i]=(val^0x80000000UL)*4.6566128730e-10;

      k+=inbytes;
    }
    if(ch==2){
      M=(buf[0][i]+buf[1][i])*.5;
      S=(buf[0][i]-buf[1][i])*.5;
      buf[0][i]=M;
      buf[1][i]=S;
    }
  }    
  primed=1;
  pthread_mutex_unlock(&master_mutex);
  return 0;
}

void PutNumLE(long num,FILE *f,int bytes){
  int i=0;
  while(bytes--){
    fputc((num>>(i<<3))&0xff,f);
    i++;
  }
}

void WriteWav(FILE *f,long channels,long rate,long bits,long duration){
  if(ftell(f)>0)
    if(fseek(f,0,SEEK_SET))
      return;
  fprintf(f,"RIFF");
  PutNumLE(duration+44-8,f,4);
  fprintf(f,"WAVEfmt ");
  PutNumLE(16,f,4);
  PutNumLE(1,f,2);
  PutNumLE(channels,f,2);
  PutNumLE(rate,f,4);
  PutNumLE(rate*channels*((bits-1)/8+1),f,4);
  PutNumLE(((bits-1)/8+1)*channels,f,2);
  PutNumLE(bits,f,2);
  fprintf(f,"data");
  PutNumLE(duration,f,4);
}

int isachr(FILE *f){
  struct stat s;

  if(!fstat(fileno(f),&s))
    if(S_ISCHR(s.st_mode)) return 1;
  return 0;
}

#define toBark(n) \
  (13.1f*atan(.00074f*(n))+2.24f*atan((n)*(n)*1.85e-8f)+1e-4f*(n))
#define todB(x)   ((x)==0?-400.f:log((x)*(x))*4.34294480f)
#define fromdB(x) (exp((x)*.11512925f))  


void _analysis(char *base,int i,double *v,int n,int bark,int dB){
  int j;
  FILE *of;
  char buffer[80];

  sprintf(buffer,"%s_%d.m",base,i);
  of=fopen(buffer,"w");
  
  if(!of)perror("failed to open data dump file");
  
  for(j=0;j<n;j++){
    if(!dB || v[j]!=0){
      if(bark)
	fprintf(of,"%f ",toBark(22050.f*j/n));
      else
	fprintf(of,"%f ",(double)j);
      
      if(dB){
	fprintf(of,"%.12f\n",todB(v[j]));
      }else{
	fprintf(of,"%.12f\n",v[j]);
      }
    }
  }
  fclose(of);
}
static int seq;

/* playback must be halted to change blocksize. */
void *playback_thread(void *dummy){
  FILE *playback_fd=NULL;
  int i,j,k;
  int format=AFMT_S16_NE;
  int channels=ch;
  int irate=rate;
  unsigned char *audiobuf;
  int audiobufsize;
  int ret,fd;
  double **buf;
  double **work;
  double **save;
  double *window;
  int bigendianp=0;
  double scale;
  int last=0;
  off_t count=0;

  pthread_mutex_lock(&master_mutex);
  block=wc.block_a;
  scale=2./block;
  eq_dirty=1;
  noise_dirty=1;

  maxtimepost=0.;
  maxtimepre=0.;
  maxtimeposthold=0.;
  maxtimeprehold=0.;
  pthread_mutex_unlock(&master_mutex);

  dyn_decaysize=MAX_DECAY_MS/(block*500./rate);
  dyn_decay=alloca(sizeof(*dyn_decay)*dyn_decaysize);
  memset(dyn_decay,0,sizeof(*dyn_decay)*dyn_decaysize);

  if(outfileno==-1){
    playback_fd=fopen("/dev/dsp","wb");
  }else{
    playback_fd=fdopen(dup(outfileno),"wb");
  }
  if(!playback_fd){
    pthread_mutex_lock(&master_mutex);
    playback_active=0;
    playback_exit=0;
    pthread_mutex_unlock(&master_mutex);
    return NULL;
  }

  /* is this file a block device? */
  if(isachr(playback_fd)){
    int fragment=0x7fff000d;

    fd=fileno(playback_fd);

    /* try to lower the DSP delay; this ioctl may fail gracefully */
    ret=ioctl(fd,SNDCTL_DSP_SETFRAGMENT,&fragment);
    if(ret){
      fprintf(stderr,"Could not set DSP fragment size; continuing.\n");
    }

    ret=ioctl(fd,SNDCTL_DSP_SETFMT,&format);
    if(ret || format!=AFMT_S16_NE){
      fprintf(stderr,"Could not set AFMT_S16_NE playback\n");
      exit(1);
    }
    ret=ioctl(fd,SNDCTL_DSP_CHANNELS,&channels);
    if(ret || channels!=ch){
      fprintf(stderr,"Could not set %d channel playback\n",ch);
      exit(1);
    }
    ret=ioctl(fd,SNDCTL_DSP_SPEED,&irate);
    if(ret || irate!=rate){
      fprintf(stderr,"Could not set %dHz playback\n",44100);
      exit(1);
    }

    if(AFMT_S16_NE==AFMT_S16_BE)bigendianp=1;

  }else{
    WriteWav(playback_fd,ch,rate,outbytes*8,-1);
  }

  buf=alloca(sizeof(*buf)*ch);
  work=alloca(sizeof(*work)*ch);
  save=alloca(sizeof(*save)*ch);
  for(i=0;i<ch;i++){
    buf[i]=alloca(sizeof(**buf)*block);
    work[i]=alloca(sizeof(**work)*block);
    save[i]=alloca(sizeof(**save)*block/2);
    memset(save[i],0,sizeof(**save)*block/2);
  }
  audiobufsize=block/2*ch*outbytes;
  audiobuf=alloca(audiobufsize);
  window=alloca(block*sizeof(*window));

  drft_init(&fft,block);

  env_init(&envpre,rate/2,block/2,.5);
  env_init(&envpost,rate/2,block/2,.5);

  for(i=0;i<block;i++)
    window[i]=sin((i+.5)/block*M_PI);

  aseek(cursor);
  pthread_mutex_lock(&master_mutex);
  primed=0;
  pthread_mutex_unlock(&master_mutex);

  while(1){
    pthread_mutex_lock(&master_mutex);
    if(playback_exit){
      pthread_mutex_unlock(&master_mutex);
      break;
    }
    pthread_mutex_unlock(&master_mutex);

    /* get data */
    if(aread(buf))break;

    for(j=0;j<ch;j++){
      double *v=buf[j];
      double *w=work[j];
 
      /* window */
      for(i=0;i<block;i++)
	w[i]=v[i]*window[i]*scale;

      /* forward FFT */
      drft_forward(&fft,w);
      
      /* noise thresh */
      noise_filter(w);
      
      /* EQ */
      eq_filter(w);
      
      /* dynamic range att */
      if(wc.dynamicatt_p){
	dynamic_sq(w);
      }
	
      /**************************/
      
      /* inverse FFT */
      drft_backward(&fft,w);

      /* master att */
      master_att(w);

    }

    /* Back to L/R if stereo */
    
    if(ch==2){
      for(i=0;i<block;i++){
	double L=(work[0][i]+work[1][i])*.5;
	double R=(work[0][i]-work[1][i])*.5;
	work[0][i]=L;
	work[1][i]=R;
      }
    }

    dynamic_limit(work);
      
    for(j=0;j<ch;j++){
      double *w=work[j];

      /* window */
      for(i=0;i<block;i++)
	w[i]*=window[i];

      /* add */
      for(i=0;i<block/2;i++)
	w[i]+=save[j][i];
      
      /* save */
      for(i=0;i<block/2;i++)
	save[j][i]=w[i+block/2];
      
    }

    k=0;
    for(i=0;i<block/2;i++){
      for(j=0;j<ch;j++){
	int val=rint(work[j][i]*32767.);
	if(val>32767)val=32767;
	if(val<-32768)val=-32768;
	if(bigendianp){
	  audiobuf[k++]=val>>8;
	  audiobuf[k++]=val;
	}else{
	  audiobuf[k++]=val;
	  audiobuf[k++]=val>>8;
	}
      }
    }
    {
      struct timeval tv;
      long foo;
      gettimeofday(&tv,NULL);
      foo=tv.tv_sec*10+tv.tv_usec/100000;
      if(last!=foo)
	write(ttypipe[1],"",1);
      last=foo;
    }

    count+=fwrite(audiobuf,1,audiobufsize,playback_fd);

  }

  if(!isachr(playback_fd))WriteWav(playback_fd,ch,rate,outbytes*8,count);

  pthread_mutex_lock(&master_mutex);
  fclose(playback_fd);
  playback_active=0;
  playback_exit=0;
  drft_clear(&fft);
  env_clear(&envpre);
  env_clear(&envpost);
  pthread_mutex_unlock(&master_mutex);
  write(ttypipe[1],"",1);
  return(NULL);
}

void save_settings(int fd){
  FILE *f;
  int i,j;

  if(fd>=0){
    lseek(fd,0,SEEK_SET);
    ftruncate(fd,0);
    f=fdopen(fd,"r+");
    
    memcpy(&configlist[configactive],&wc,sizeof(wc));

    for(i=0;i<CONFIG_MAX;i++){
      if(configlist[i].used){
	
	fprintf(f,"PROFILE:%d\n",i);
	
	fprintf(f,"BLOCK:%ld\n",configlist[i].block_a);
	fprintf(f,"NOISE_P:%ld\n",configlist[i].noise_p);
	fprintf(f,"NOISE_T:");
	for(j=0;j<BANDS;j++)fprintf(f,"%ld:",configlist[i].noiset[j]);
	fprintf(f,"\n");
	fprintf(f,"EQ_P:%ld\n",configlist[i].eq_p);
	fprintf(f,"EQ_T:");
	for(j=0;j<BANDS;j++)fprintf(f,"%ld:",configlist[i].eqt[j]);
	fprintf(f,"\n");
	
	fprintf(f,"DYN_P:%ld\n",configlist[i].dyn_p);
	fprintf(f,"DYN_T:%ld\n",configlist[i].dynt);
	fprintf(f,"DYN_MS:%ld\n",configlist[i].dynms);
	
	fprintf(f,"MASTERATT:%ld\n",configlist[i].masteratt);
	fprintf(f,"DYNAMICATT:%ld\n",configlist[i].dynamicatt);
	
	fprintf(f,"MASTERATT_P:%ld\n",configlist[i].masteratt_p);
	fprintf(f,"DYNAMICATT_P:%ld\n",configlist[i].dynamicatt_p);
	
      }
    }
    
    fprintf(f,"CONFIG_ACTIVE:%ld\n",configactive);
    
    fprintf(f,"A:%ld\n",A);
    fprintf(f,"B:%ld\n",B);
    fprintf(f,"NM:%ld\n",n_m);
    fprintf(f,"EQM:%ld\n",eq_m);
    for(i=0;i<10;i++)
      fprintf(f,"TIME_%d:%ld\n",i,TX[i]);
  
    fclose(f);
  }
}

void confparse_ld(char *line,char *match,long *assign){
  char buffer[160];
  snprintf(buffer,160,"%s:%%ld",match);
  sscanf(line,buffer,assign);
}
void confparse_f(char *line,char *match,double *assign){
  char buffer[160];
  snprintf(buffer,160,"%s:%%f",match);
  sscanf(line,buffer,assign);
}
void confparse_s(char *line,char *match,char *assign,int len){
  char buffer[160];
  snprintf(buffer,160,"%s:%%s",match);
  if(strlen(buffer)-strlen(match)-3<(unsigned)len)
    sscanf(line,buffer,assign);
}
void confparse_bands(char *line,char *match,long *assign){
  char buffer[320];
  snprintf(buffer,320,
	   "%s:%%ld:%%ld:%%ld:%%ld:%%ld:%%ld:%%ld:%%ld:%%ld:"
	      "%%ld:%%ld:%%ld:%%ld:%%ld:%%ld:%%ld:%%ld:%%ld:"
	      "%%ld:%%ld:%%ld:%%ld:%%ld:%%ld:%%ld:%%ld:%%ld:"
	      "%%ld:%%ld:%%ld:%%ld:%%ld:%%ld:%%ld:%%ld:",match);
  sscanf(line,buffer,
	 assign+0,assign+1,assign+2,assign+3,
	 assign+4,assign+5,assign+6,assign+7,
	 assign+8,assign+9,assign+10,assign+11,
	 assign+12,assign+13,assign+14,assign+15,
	 assign+16,assign+17,

	 assign+18,assign+19,assign+20,assign+21,
	 assign+22,assign+23,assign+24,assign+25,
	 assign+26,assign+27,assign+28,assign+29,
	 assign+30,assign+31,assign+32,assign+33,
	 assign+34);

}

void load_settings(int fd){
  char buffer[1024];
  long i=0;
  FILE *f;
  lseek(fd,0,SEEK_SET);
  f=fdopen(fd,"r");
  
  while(fgets(buffer,1024,f)){

    confparse_ld(buffer,"PROFILE",&i);

    if(i>=0 && i< CONFIG_MAX){

      configlist[i].used=1;

      confparse_ld(buffer,"BLOCK",&configlist[i].block_a);
      confparse_ld(buffer,"NOISE_P",&configlist[i].noise_p);
      confparse_bands(buffer,"NOISE_T",configlist[i].noiset);
      confparse_ld(buffer,"EQ_P",&configlist[i].eq_p);
      confparse_bands(buffer,"EQ_T",configlist[i].eqt);
      
      confparse_ld(buffer,"DYN_P",&configlist[i].dyn_p);
      confparse_ld(buffer,"DYN_T",&configlist[i].dynt);
      confparse_ld(buffer,"DYN_MS",&configlist[i].dynms);
    
      confparse_ld(buffer,"MASTERATT",&configlist[i].masteratt);
      confparse_ld(buffer,"DYNAMICATT",&configlist[i].dynamicatt);
      confparse_ld(buffer,"MASTERATT_P",&configlist[i].masteratt_p);
      confparse_ld(buffer,"DYNAMICATT_P",&configlist[i].dynamicatt_p);
      
    }

    confparse_ld(buffer,"NM",&n_m);
    confparse_ld(buffer,"EQM",&eq_m);
    confparse_ld(buffer,"A",&A);
    confparse_ld(buffer,"B",&B);

    confparse_ld(buffer,"TIME_0",&TX[0]);
    confparse_ld(buffer,"TIME_1",&TX[1]);
    confparse_ld(buffer,"TIME_2",&TX[2]);
    confparse_ld(buffer,"TIME_3",&TX[3]);
    confparse_ld(buffer,"TIME_4",&TX[4]);
    confparse_ld(buffer,"TIME_5",&TX[5]);
    confparse_ld(buffer,"TIME_6",&TX[6]);
    confparse_ld(buffer,"TIME_7",&TX[7]);
    confparse_ld(buffer,"TIME_8",&TX[8]);
    confparse_ld(buffer,"TIME_9",&TX[9]);
    confparse_ld(buffer,"CONFIG_ACTIVE",&configactive);

  }

  Acursor=time_to_cursor(A);
  Bcursor=time_to_cursor(B);
  if(Acursor<0)Acursor=0;
  if(Acursor>file_list[file_entries-1].end)Acursor=file_list[file_entries-1].end;
  if(Bcursor>file_list[file_entries-1].end)Bcursor=file_list[file_entries-1].end;
  
  memcpy(&wc,&configlist[configactive],sizeof(wc));

}

void *tty_thread(void *dummy){
  char buf;

  while(1){
    int ret=read(ttyfd,&buf,1);
    if(ret==1){
      write(ttypipe[1],&buf,1);
    }
  }

}

int main(int argc, char **argv){
  off_t total=0;
  int i,j;
  int configfd;
  int stdinp=0;
  char *fname="stdin";

  /* parse command line and open all the input files */
  if(argc==1){
    /* look at stdin... is it a file, pipe, tty...? */
    if(isatty(STDIN_FILENO)){
      fprintf(stderr,
	      "Postfish requires input either as a list of contiguous WAV\n"
	      "files on the command line, or WAV data piped|redirected to\n"
	      "stdin.\n");
      exit(1);
    }
    stdinp=1;    /* file coming in via stdin */
    file_entries=1;
  }else
    file_entries=argc-1;

  file_list=calloc(file_entries,sizeof(file_entry));
  for(i=1;i<=file_entries;i++){
    FILE *f;
    
    if(stdinp){
      int newfd=dup(STDIN_FILENO);
      f=fdopen(newfd,"rb");
    }else{
      f=fopen(argv[i],"rb");
      fname=argv[i];
    }

    if(f){
      unsigned char buffer[81];
      off_t filelength;
      int datap=0;
      int fmtp=0;
      file_list[i-1].f=f;
      
      /* parse header (well, sort of) and get file size */
      seekable=(fseek(f,0,SEEK_CUR)?0:1);
      if(!seekable){
	filelength=-1;
      }else{
	fseek(f,0,SEEK_END);
	filelength=ftello(f);
	fseek(f,0,SEEK_SET);
      }

      fread(buffer,1,12,f);
      if(strncmp(buffer,"RIFF",4) || strncmp(buffer+8,"WAVE",4)){
	fprintf(stderr,"%s: Not a WAVE file.\n",fname);
	exit(1);
      }

      while(fread(buffer,1,8,f)==8){
	unsigned long chunklen=
	  buffer[4]|(buffer[5]<<8)|(buffer[6]<<16)|(buffer[7]<<24);

	if(!strncmp(buffer,"fmt ",4)){
	  int ltype;
	  int lch;
	  int lrate;
	  int lbits;

	  if(chunklen>80){
	    fprintf(stderr,"%s: WAVE file fmt chunk too large to parse.\n",fname);
	    exit(1);
	  }
	  fread(buffer,1,chunklen,f);
	  
	  ltype=buffer[0] |(buffer[1]<<8);
	  lch=  buffer[2] |(buffer[3]<<8);
	  lrate=buffer[4] |(buffer[5]<<8)|(buffer[6]<<16)|(buffer[7]<<24);
	  lbits=buffer[14]|(buffer[15]<<8);

	  if(ltype!=1){
	    fprintf(stderr,"%s: WAVE file not PCM.\n",fname);
	    exit(1);
	  }

	  if(i==1){
	    ch=lch;
	    rate=lrate;
	    inbytes=lbits/8;
	    if(inbytes>1)signp=1;
	  }else{
	    if(ch!=lch){
	      fprintf(stderr,"%s: WAVE files must all have same number of channels.\n",fname);
	      exit(1);
	    }
	    if(rate!=lrate){
	      fprintf(stderr,"%s: WAVE files must all be same sampling rate.\n",fname);
	      exit(1);
	    }
	    if(inbytes!=lbits/8){
	      fprintf(stderr,"%s: WAVE files must all be same sample width.\n",fname);
	      exit(1);
	    }
	  }
	  fmtp=1;
	} else if(!strncmp(buffer,"data",4)){
	  off_t pos=ftello(f);
	  if(!fmtp){
	    fprintf(stderr,"%s: WAVE fmt chunk must preceed data chunk.\n",fname);
	    exit(1);
	  }
	  datap=1;
	  
	  if(seekable)
	    filelength=(filelength-pos)/(ch*inbytes)*(ch*inbytes)+pos;

	  if(chunklen==0UL ||
	     chunklen==0x7fffffffUL || 
	     chunklen==0xffffffffUL){
	    file_list[i-1].begin=total;
	    total=file_list[i-1].end=0;
	    fprintf(stderr,"%s: Incomplete header; assuming stream.\n",fname);
	  }else if(filelength==-1 || chunklen+pos<=filelength){
	    file_list[i-1].begin=total;
	    total=file_list[i-1].end=total+chunklen;
	    fprintf(stderr,"%s: Using declared file size.\n",fname);
	  }else{
	    file_list[i-1].begin=total;
	    total=file_list[i-1].end=total+filelength-pos;
	    fprintf(stderr,"%s: Using actual file size.\n",fname);
	  }
	  file_list[i-1].data=ftello(f);
	  
	  break;
	} else {
	  fprintf(stderr,"%s: Unknown chunk type %c%c%c%c; skipping.\n",fname,
		  buffer[0],buffer[1],buffer[2],buffer[3]);
	  for(j=0;j<(int)chunklen;j++)
	    if(fgetc(f)==EOF)break;
	}
      }

      if(!datap){
	fprintf(stderr,"%s: WAVE file has no data chunk.\n",fname);
	exit(1);
      }
      
    }else{
      fprintf(stderr,"%s: Unable to open file.\n",fname);
      exit(1);
    }
  }

  /* look at stdout... do we have a file or device? */
  if(!isatty(STDOUT_FILENO)){
    /* apparently; assume this is the file/device for output */
    loop_flag=0;
    outfileno=dup(STDOUT_FILENO);
    dup2(STDERR_FILENO,STDOUT_FILENO);
  }


  /* load config */
  {
    configfd=open(".postfishrc",O_RDWR|O_CREAT,0666);
    if(configfd>=0)load_settings(configfd);
  }

  /* set up the hack for interthread ncurses event triggering through
   input subversion */
  ttyfd=open("/dev/tty",O_RDONLY);

  if(ttyfd<0){
    fprintf(stderr,"Unable to open /dev/tty:\n"
            "  %s\n",strerror(errno));
    
    exit(1);
  }
  if(pipe(ttypipe)){
    fprintf(stderr,"Unable to open tty pipe:\n"
            "  %s\n",strerror(errno));
    
    exit(1);
  }
  dup2(ttypipe[0],0);

  pthread_create(&tty_thread_id,NULL,tty_thread,NULL);

  aseek(0);

  {
    int ysize=11+BANDS;
    int xsize=104;

    initscr(); cbreak(); noecho();
    nonl();
    intrflush(stdscr, FALSE);
    keypad(stdscr, TRUE);
    use_default_colors();
    clear();
    def_prog_mode();           /* save current tty modes */
    refresh();
    clear();

    if(LINES<ysize || COLS<xsize){
      endwin();
      fprintf(stderr,"Minimum required terminal size: %d cols x %d lines\n",
    	      xsize, ysize);
      exit(1);
    }

    form_init(&editf,120,1);
    form_init(&noneditf,50,0);
    box(stdscr,0,0);
    mvaddstr(0, 2, " Postfish Filter $Id: postfish.c,v 1.6 2003/09/16 08:55:12 xiphmont Exp $ ");
    mvaddstr(LINES-1, 2, 
	     "  [<]<<   [,]<   [Spc] Play/Pause   [Bksp] Stop/Cue   [.]>   [>]>>  ");

    mvaddstr(LINES-1, COLS-12, " [q] Quit ");

    update_static_N();
    update_N();
    
    update_static_E();
    update_E();

    update_static_D();
    update_D();

    update_a();
    update_b();
    update_C();
    update_play(0);
    update_static_0();

    refresh();

    signal(SIGINT,SIG_IGN);

    while(1){
      int c=form_handle_char(&editf,pgetch());
      if(c=='q')break;
      switch(c){
      case 'N':
	n_m=!n_m;
	update_N();
	break;
      case 'E':
	eq_m=!eq_m;
	update_E();
	break;
      case '<':
	pthread_mutex_lock(&master_mutex);
	aseek(cursor-rate*ch*inbytes*60);
	pthread_mutex_unlock(&master_mutex);
	update_ui(0);
	break;
      case ',':
	pthread_mutex_lock(&master_mutex);
	aseek(cursor-rate*ch*inbytes*5);
	pthread_mutex_unlock(&master_mutex);
	update_ui(0);
	break;
      case '>':
	pthread_mutex_lock(&master_mutex);
	aseek(cursor+rate*ch*inbytes*60);
	pthread_mutex_unlock(&master_mutex);
	update_ui(0);
	break;
      case '.':
	pthread_mutex_lock(&master_mutex);
	aseek(cursor+rate*ch*inbytes*5);
	pthread_mutex_unlock(&master_mutex);
	update_ui(0);
	break;
      case 'n':
	pthread_mutex_lock(&master_mutex);
	wc.noise_p=!wc.noise_p;
	pthread_mutex_unlock(&master_mutex);
	update_N();
	break;
      case 'e':
	pthread_mutex_lock(&master_mutex);
	wc.eq_p=!wc.eq_p;
	pthread_mutex_unlock(&master_mutex);
	update_E();
	break;
      case 'l':
	pthread_mutex_lock(&master_mutex);
	wc.dyn_p++;
	if(wc.dyn_p>2)wc.dyn_p=0;
	pthread_mutex_unlock(&master_mutex);
	update_D();
	break;
      case 'm':
	pthread_mutex_lock(&master_mutex);
	wc.masteratt_p=!wc.masteratt_p;
	pthread_mutex_unlock(&master_mutex);
	update_D();
	break;
      case 'r':
	pthread_mutex_lock(&master_mutex);
	wc.dynamicatt_p=!wc.dynamicatt_p;
	pthread_mutex_unlock(&master_mutex);
	update_D();
	break;
      case '0':case '1':case '2':case '3':case '4':
      case '5':case '6':case '7':case '8':case '9':
	{
	  int num=c-48;
	  if(TX[num]!=-1){
	    pthread_mutex_lock(&master_mutex);
	    aseek(time_to_cursor(TX[num]));
	    pthread_mutex_unlock(&master_mutex);
	    update_play(0);
	  }else{
	    pthread_mutex_lock(&master_mutex);
	    TX[num]=cursor_to_time(cursor);
	    pthread_mutex_unlock(&master_mutex);
	    update_0();
	  }
	}
	update_ui(0);
	break;
      case ')':
	TX[0]=-1;update_0();break;
      case '!':
	TX[1]=-1;update_0();break;
      case '@':
	TX[2]=-1;update_0();break;
      case '#':
	TX[3]=-1;update_0();break;
      case '$':
	TX[4]=-1;update_0();break;
      case '%':
	TX[5]=-1;update_0();break;
      case '^':
	TX[6]=-1;update_0();break;
      case '&':
	TX[7]=-1;update_0();break;
      case '*':
	TX[8]=-1;update_0();break;
      case '(':
	TX[9]=-1;update_0();break;
      case 'a':
	pthread_mutex_lock(&master_mutex);
	if(Acursor==0){
	  Acursor=cursor;
	  pthread_mutex_unlock(&master_mutex);
	  A=cursor_to_time(cursor);
	  if(B<A){
	    pthread_mutex_lock(&master_mutex);
	    B=-1;
	    Bcursor=-1;
	    pthread_mutex_unlock(&master_mutex);
	    update_b();
	  }
	  update_a();
	}else{
	  aseek(Acursor);
	  pthread_mutex_unlock(&master_mutex);
	  update_play(0);
	}
	break;	    
      case 'b':
	pthread_mutex_lock(&master_mutex);
	if(Bcursor==-1){
	  Bcursor=cursor;
	  pthread_mutex_unlock(&master_mutex);
	  B=cursor_to_time(cursor);
	  if(B<A){
	    pthread_mutex_lock(&master_mutex);
	    B=-1;
	    Bcursor=-1;
	    pthread_mutex_unlock(&master_mutex);
	  }
	  update_b();
	}else
	  pthread_mutex_unlock(&master_mutex);
	  update_play(0);
	break;
      case 'A':
	pthread_mutex_lock(&master_mutex);
	Acursor=0;
	pthread_mutex_unlock(&master_mutex);
	A=0;
	update_a();
	break;
      case 'B':
	pthread_mutex_lock(&master_mutex);
	Bcursor=-1;
	pthread_mutex_unlock(&master_mutex);
	B=-1;
	update_b();
	break;
      case '\b':case KEY_BACKSPACE:case KEY_DC:
	pthread_mutex_lock(&master_mutex);
	if(playback_active){
	  playback_exit=1;
	  pthread_mutex_unlock(&master_mutex);
	  sched_yield();
	  while(1){
	    pthread_mutex_lock(&master_mutex);
	    if(playback_active){
	      pthread_mutex_unlock(&master_mutex);
	      sched_yield();
	    }else
	      break;
	  }
	}
	aseek(Acursor);
	pthread_mutex_unlock(&master_mutex);
	update_play(0);
	break;
      case ' ':
	pthread_mutex_lock(&master_mutex);
	if(!playback_active){
	  playback_active=1;
	  pthread_mutex_unlock(&master_mutex);
	  update_play(0);
	  pthread_create(&playback_thread_id,NULL,&playback_thread,NULL);
	}else{
	  playback_exit=1;
	  pthread_mutex_unlock(&master_mutex);
	  sched_yield();
	  while(1){
	    pthread_mutex_lock(&master_mutex);
	    if(playback_active){
	      pthread_mutex_unlock(&master_mutex);
	      sched_yield();
	    }else
	      break;
	  }
	  pthread_mutex_unlock(&master_mutex);
	  update_play(0);
	}
	break;
      case 'v':case 'w':case 'x':case 'y':case 'z':
	{
	  int num=c-118;
	  memcpy(configlist+configactive,&wc,sizeof(wc));
	  if(!configlist[num].used){
	    memcpy(configlist+num,&wc,sizeof(wc));
	    configlist[num].used=1;
	  }
	  pthread_mutex_lock(&master_mutex);
	  memcpy(&wc,configlist+num,sizeof(wc));
	  eq_dirty=1;
	  noise_dirty=1;
	  configactive=num;
	  pthread_mutex_unlock(&master_mutex);
	  update_C();
	  update_N();
	  update_E();
	  update_ui(0);
	  form_redraw(&editf);
	  form_redraw(&noneditf);
	  update_D();
	}
	break;
      case 'V':case 'W':case 'X':case 'Y':case 'Z':
	{
	  int num=c-86;
	  memcpy(configlist+num,&configdefault,sizeof(configdefault));
	  configlist[configactive].used=1;
	  if(configactive==num){
	    pthread_mutex_lock(&master_mutex);
	    memcpy(&wc,configlist+configactive,sizeof(wc));
	    eq_dirty=1;
	    noise_dirty=1;
	    configactive=num;
	    pthread_mutex_unlock(&master_mutex);
	  }
	  update_C();
	  update_N();
	  update_E();
	  update_ui(0);
	  form_redraw(&editf);
	  form_redraw(&noneditf);
	  update_D();
	}
	break;
      case 0:
	update_play(1);
	break;
      default:
	update_ui(0);
	break;
      }
    }
  }

  pthread_mutex_lock(&master_mutex);
  playback_exit=1;
  pthread_mutex_unlock(&master_mutex);

  while(1){
    pthread_mutex_lock(&master_mutex);
    if(playback_active){
      pthread_mutex_unlock(&master_mutex);
      sched_yield();
    }else
      break;
  }
  endwin();
  save_settings(configfd);
  if(configfd>=0)close(configfd);
  return(0);
}
