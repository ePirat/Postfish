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

#define todB(x)   ((x)==0?-400.f:log((x)*(x))*4.34294480f)
#define fromdB(x) (exp((x)*.11512925f))  
#define toOC(n)     (log(n)*1.442695f-5.965784f)

#define MAX_BLOCKSIZE 32768
#define BANDS 35

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
  char name[20];

} configprofile;

#define CONFIG_MAX 5
static long configactive=0;
static configprofile configlist[CONFIG_MAX]={
  {
    8192,1,300,-60,0,1,0,1,1,
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
     0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    1,
    {-120,-120,-120,-120,-120,-120,-120,-120,-120,
     -120,-120,-120,-120,-120,-120,-120,-120,-120,
     -120,-120,-120,-120,-120,-120,-120,-120,-120,
     -120,-120,-120,-120,-120,-120,-120,-120},
    1,"default"
  }
};
static configprofile configdefault={
  8192,1,300,-60,0,1,0,1,1,
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  1,
  {-120,-120,-120,-120,-120,-120,-120,-120,-120,
   -120,-120,-120,-120,-120,-120,-120,-120,-120,
   -120,-120,-120,-120,-120,-120,-120,-120,-120,
   -120,-120,-120,-120,-120,-120,-120,-120},
  0,"default"
};
static configprofile wc;

/* working space */

static long block=8192;

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
pthread_t playback_thread_id;


int ttyfd;
int ttypipe[2];
pthread_t tty_thread_id;

typedef struct {
  char *name;
  FILE *f;
  
  off_t begin;
  off_t end;
  off_t data;

} file_entry;

file_entry *file_list=NULL;
int file_entries=0;
int current_file_entry_number=-1;
file_entry *current_file_entry=NULL;

/* -------------------------------------- */

#define N_Y 2
#define N_X 2

#define E_Y 2
#define E_X 36

#define D_Y 2
#define D_X 70

#define T_Y (6+BANDS)
#define T_X 6

#define C_Y (BANDS-CONFIG_MAX+2)
#define C_X D_X

form editf;
form noneditf;


void update_N(){
  mvaddstr(N_Y,N_X+4,"     [n] Noise Filter ");
  if(wc.noise_p){
    attron(A_BOLD);
    addstr("ON ");
  }else{
    attron(A_BOLD);
    addstr("OFF");
  }

  attrset(0);

}

void update_static_N(){
  int i;
  mvaddstr(N_Y+2, N_X,"  63 ");
  mvaddstr(N_Y+4, N_X,"  88 ");
  mvaddstr(N_Y+6, N_X," 125 ");
  mvaddstr(N_Y+8, N_X," 175 ");
  mvaddstr(N_Y+10,N_X," 250 ");
  mvaddstr(N_Y+12,N_X," 350 ");
  mvaddstr(N_Y+14,N_X," 500 ");
  mvaddstr(N_Y+16,N_X," 700 ");
  mvaddstr(N_Y+18,N_X,"  1k ");
  mvaddstr(N_Y+20,N_X,"1.4k ");
  mvaddstr(N_Y+22,N_X,"  2k ");
  mvaddstr(N_Y+24,N_X,"2.8k ");
  mvaddstr(N_Y+26,N_X,"  4k ");
  mvaddstr(N_Y+28,N_X,"5.6k ");
  mvaddstr(N_Y+30,N_X,"  8k ");
  mvaddstr(N_Y+32,N_X," 11k ");
  mvaddstr(N_Y+34,N_X," 16k ");
  mvaddstr(N_Y+36,N_X," 22k ");

  mvvline(N_Y+2,N_X+9,0,BANDS);
  mvvline(N_Y+2,N_X+20,0,BANDS);
  mvvline(N_Y+2,N_X+31,0,BANDS);

  mvhline(N_Y+1,N_X+9,0,23);
  mvhline(N_Y+2+BANDS,N_X+9,0,23);

  mvaddch(N_Y+1,N_X+9,ACS_ULCORNER);
  mvaddch(N_Y+1,N_X+31,ACS_URCORNER);
  mvaddch(N_Y+1,N_X+20,ACS_TTEE);
  mvaddch(N_Y+2+BANDS,N_X+9,ACS_LLCORNER);
  mvaddch(N_Y+2+BANDS,N_X+31,ACS_LRCORNER);

  mvaddstr(N_Y+2+BANDS,N_X,"       dB");
  mvaddstr(N_Y+2+BANDS,N_X+10,"30- ");
  mvaddstr(N_Y+2+BANDS,N_X+19," 0 ");
  mvaddstr(N_Y+2+BANDS,N_X+27," +30");

  for(i=0;i<BANDS;i++)
    field_add(&editf,FORM_DB,N_X+5,N_Y+2+i,4,0,&wc.noiset[i],&noise_dirty,0,-150,0);
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
}

void update_static_E(){
  int i;
  mvaddstr(E_Y+2, E_X,"  63 ");
  mvaddstr(E_Y+4, E_X,"  88 ");
  mvaddstr(E_Y+6, E_X," 125 ");
  mvaddstr(E_Y+8, E_X," 175 ");
  mvaddstr(E_Y+10,E_X," 250 ");
  mvaddstr(E_Y+12,E_X," 350 ");
  mvaddstr(E_Y+14,E_X," 500 ");
  mvaddstr(E_Y+16,E_X," 700 ");
  mvaddstr(E_Y+18,E_X,"  1k ");
  mvaddstr(E_Y+20,E_X,"1.4k ");
  mvaddstr(E_Y+22,E_X,"  2k ");
  mvaddstr(E_Y+24,E_X,"2.8k ");
  mvaddstr(E_Y+26,E_X,"  4k ");
  mvaddstr(E_Y+28,E_X,"5.6k ");
  mvaddstr(E_Y+30,E_X,"  8k ");
  mvaddstr(E_Y+32,E_X," 11k ");
  mvaddstr(E_Y+34,E_X," 16k ");
  mvaddstr(E_Y+36,E_X," 22k ");

  mvvline(E_Y+2,E_X+8,0,BANDS);
  mvvline(E_Y+2,E_X+30,0,BANDS);

  mvhline(E_Y+1,E_X+8,0,23);
  mvhline(E_Y+2+BANDS,E_X+8,0,23);

  mvaddch(E_Y+1,E_X+8,ACS_ULCORNER);
  mvaddch(E_Y+1,E_X+30,ACS_URCORNER);
  mvaddch(E_Y+2+BANDS,E_X+8,ACS_LLCORNER);
  mvaddch(E_Y+2+BANDS,E_X+30,ACS_LRCORNER);

  mvaddstr(E_Y+2+BANDS,E_X,"      dB");
  mvaddstr(E_Y+2+BANDS,E_X+9,"30- ");
  mvaddstr(E_Y+2+BANDS,E_X+18," 0 ");
  mvaddstr(E_Y+2+BANDS,E_X+26," +30");

  mvaddstr(E_Y+1,E_X+9,"120- ");
  mvaddstr(E_Y+1,E_X+27," +0");

  for(i=0;i<BANDS;i++)
    field_add(&editf,FORM_DB,E_X+5,E_Y+2+i,3,1,&wc.eqt[i],&eq_dirty,0,-30,30);
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
  
  move(D_Y+8,D_X+18);
  if(wc.dyn_p)
    addstr("ON ");
  else
    addstr("OFF");

  attrset(0);
}

static formfield *A_field;
static formfield *B_field;
static formfield *T_field;

static formfield *pre_field;
static formfield *post_field;
static long pre_var=0;
static long post_var=0;

void update_static_D(){

  mvaddstr(D_Y+0,D_X,"[m]    Master Att            dB");
  mvaddstr(D_Y+1,D_X,"[r] Dynamic Range            db");
  mvaddstr(D_Y+2,D_X,"       Frame size");

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

  mvaddstr(D_Y+8,D_X,"[l] Dynamic Limit            dB");
  mvaddstr(D_Y+9,D_X,"    peak integral            ms");

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

  field_add(&editf,FORM_DB,D_X+23,D_Y+0,5,2,&wc.masteratt,NULL,1,-900,+900);
  field_add(&editf,FORM_DB,D_X+23,D_Y+1,5,3,&wc.dynamicatt,NULL,1,-900,+900);
  field_add(&editf,FORM_P2,D_X+23,D_Y+2,5,4,&wc.block_a,NULL,0,64,MAX_BLOCKSIZE);
  field_add(&editf,FORM_DB,D_X+23,D_Y+8,5,5,&wc.dynt,NULL,1,-300,0);
  field_add(&editf,FORM_DB,D_X+23,D_Y+9,5,6,&wc.dynms,NULL,0,0,MAX_DECAY_MS);

  mvaddstr(D_Y+14,D_X,"[a]             [A] Clear");
  mvaddstr(D_Y+15,D_X,"[b]             [B] Clear");

  A_field=field_add(&editf,FORM_TIME,D_X+4,D_Y+14,11,9,&A,NULL,0,0,99999999);
  B_field=field_add(&editf,FORM_TIME,D_X+4,D_Y+15,11,10,&B,NULL,0,0,99999999);
  noneditf.cursor=-1;

  T_field=field_add(&noneditf,FORM_TIME,D_X+4,D_Y+16,11,0,&T,NULL,0,0,99999999);

  pre_field=field_add(&noneditf,FORM_DB,D_X+23,D_Y+5,5,0,&pre_var,NULL,0,-30,30);
  post_field=field_add(&noneditf,FORM_DB,D_X+23,D_Y+11,5,0,&post_var,NULL,0,-30,0);

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
  field_add(&noneditf,FORM_TIME,T_X+3,T_Y,11,0,&TX[0],NULL,0,0,99999999);
  mvaddstr(T_Y,T_X+19,"[1>");
  field_add(&noneditf,FORM_TIME,T_X+21,T_Y,11,0,&TX[1],NULL,0,0,99999999);
  mvaddstr(T_Y,T_X+38,"[2>");
  field_add(&noneditf,FORM_TIME,T_X+41,T_Y,11,0,&TX[2],NULL,0,0,99999999);
  mvaddstr(T_Y,T_X+57,"[3>");
  field_add(&noneditf,FORM_TIME,T_X+60,T_Y,11,0,&TX[3],NULL,0,0,99999999);
  mvaddstr(T_Y,T_X+76,"[4>");
  field_add(&noneditf,FORM_TIME,T_X+79,T_Y,11,0,&TX[4],NULL,0,0,99999999);

  mvaddstr(T_Y+1,T_X,"[5>");
  field_add(&noneditf,FORM_TIME,T_X+3,T_Y+1,11,0,&TX[5],NULL,0,0,99999999);
  mvaddstr(T_Y+1,T_X+19,"[6>");
  field_add(&noneditf,FORM_TIME,T_X+21,T_Y+1,11,0,&TX[6],NULL,0,0,99999999);
  mvaddstr(T_Y+1,T_X+38,"[7>");
  field_add(&noneditf,FORM_TIME,T_X+41,T_Y+1,11,0,&TX[7],NULL,0,0,99999999);
  mvaddstr(T_Y+1,T_X+57,"[8>");
  field_add(&noneditf,FORM_TIME,T_X+60,T_Y+1,11,0,&TX[8],NULL,0,0,99999999);
  mvaddstr(T_Y+1,T_X+76,"[9>");
  field_add(&noneditf,FORM_TIME,T_X+79,T_Y+1,11,0,&TX[9],NULL,0,0,99999999);

}

void update_a(){
  draw_field(A_field);
}

void update_b(){
  draw_field(B_field);
}

off_t time_to_cursor(long t){
  if(t<0)
    return(-1);

  {
    off_t c=t%10000;
    
    c+=t/10000%100*6000;
    c+=t/1000000*600000;
    
    return((off_t)rint(c*.01*rate)*ch*inbytes);
  }
}

long cursor_to_time(off_t c){
  long T;
  if(c<0)return(-1);
  c=c*100./rate/ch/inbytes;
  T =c/(100*60*60)*1000000;
  T+=c/(100*60)%60*10000;
  T+=c/(100)%60*100;
  T+=c%100;
  return(T);
}

void update_ui(){
  int i,j;

  pthread_mutex_lock(&master_mutex); 
  T=cursor_to_time(cursor);
  pthread_mutex_unlock(&master_mutex); 
  draw_field(T_field);

  for(i=0;i<BANDS;i++){
    int valM,valA,valT;
    valT=rint(wc.eqt[i]/3.+10);
    pthread_mutex_lock(&master_mutex);
    valM=rint(todB(eqt_feedbackmax[i])/5.7142857+21);
    valA=rint(todB(eqt_feedbackav[i]/eqt_feedbackcount[i])/5.7142857+21);
    pthread_mutex_unlock(&master_mutex);

    move(E_Y+2+i,E_X+9);
    for(j=0;j<valA && j<21;j++){
      if(j==valT)
	addch(ACS_VLINE);
      else
	addch(' ');
    }

    for(;j<=valM && j<21;j++){
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

void update_play(){
  int i,j;
  pthread_mutex_lock(&master_mutex); 
  update_ui();
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
  
  for(i=0;i<BANDS;i++){
    int valM,valA;
    pthread_mutex_lock(&master_mutex);

    if(noiset_feedbackcount[i]){
      valM=rint(todB(noiset_feedbackmax[i])/3.+10);
      valA=rint(todB(noiset_feedbackav[i]/noiset_feedbackcount[i])/3.+10);
    }else{
      valM=-1;
      valA=-1;
    }
    pthread_mutex_unlock(&master_mutex);

    move(N_Y+2+i,N_X+10);
    for(j=0;j<valA && j<=21;j++){
      if(j==21)
	addch('+');
      else
	if(j==10)
	  addch(ACS_VLINE);
	else
	  addch(' ');
    }

    for(;j<=valM && j<=21;j++){
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


    noiset_feedbackav[i]/=2;
    noiset_feedbackmax[i]/=2;
    noiset_feedbackcount[i]/=2;
  }

  for(i=0;i<BANDS;i++){
    eqt_feedbackav[i]/=2;
    eqt_feedbackmax[i]/=2;
    eqt_feedbackcount[i]/=2;
  }

  /* pre-limit bargraph */
  {
    double val;
    int ival;
    move(D_Y+5,D_X+1);
    pthread_mutex_lock(&master_mutex);
    if(maxtimeprehold){
      val=maxtimepre;
      pre_var=rint(todB(maxtimeprehold));
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
      
      maxtimepre/=2;
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
      post_var=rint(todB(maxtimeposthold));
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
      
      maxtimepost/=2;
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

/* return a scaled amplitude */
inline double atan_amplitude(double amplitude,double thresh){
  if(fabs(amplitude)>thresh){
    /* use a scaled atan() rolloff, which will begin with a 1:1 slope
       but asymptotically approach our limit */
    double scale=(1./(M_PI*.5))*(1.-thresh);
    if(amplitude>0)
      return(atan((amplitude-thresh)/scale)*scale + thresh );
    else
      return(atan((amplitude+thresh)/scale)*scale - thresh );
  }else
    return(amplitude);
}

inline double atan_scale(double amplitude,double thresh){
  if(amplitude==0.)return 1.;
  return(atan_amplitude(amplitude,thresh)/amplitude);
}

/* limit output by block; atan-scale according to max amplitude */
void dynamic_limit(double **b){
  int i,j;
  pthread_mutex_lock(&master_mutex);
  {
    double thresh=fromdB(wc.dynt*.1);
    double y=0;
    double d=0;
    long ms=wc.dynms;
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

    d=atan_scale(y,thresh);
    y=dyn_decay[0];

    pthread_mutex_lock(&master_mutex);
    if(maxtimepre<y)maxtimepre=y;
    if(maxtimepost<y*d)maxtimepost=y*d;
    if(maxtimeprehold<y)maxtimeprehold=y;
    if(maxtimeposthold<y*d)maxtimeposthold=y*d;
    if(wc.dyn_p && d!=1.){
      pthread_mutex_unlock(&master_mutex);

      for(j=0;j<ch;j++)
	for(i=0;i<block;i++)
	  b[j][i]*=d;
    }else{
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
    dB=set[ihoc]*(1.-dhoc)+set[ihoc+1]*dhoc;

    t[i]=fromdB(dB);
  }

}

void noise_filter(double *b){
  int i;
  double dv;
  pthread_mutex_lock(&master_mutex);
  if(wc.noise_p){
    if(noise_dirty){
      refresh_thresh_array(noise_tfull,wc.noiset);
      noise_dirty=0;
    }
      
    pthread_mutex_unlock(&master_mutex);
    
    dv=fabs(b[0])/noise_tfull[0];
    if(noiset_feedbackmax[oc[0]]<dv)noiset_feedbackmax[oc[0]]=dv;
    noiset_feedbackav[oc[0]]+=dv;
    noiset_feedbackcount[oc[0]]++;

    if(b[0]<0){
      b[0]+=noise_tfull[0];
      if(b[0]>0)b[0]=0;
    }else{
      b[0]-=noise_tfull[0];
      if(b[0]<0)b[0]=0;
    }

    for(i=1;i<block/2;i++){
      double M=sqrt(b[i*2]*b[i*2]+b[i*2-1]*b[i*2-1]);
      
      dv=M/noise_tfull[i];
      if(noiset_feedbackmax[oc[i]]<dv)noiset_feedbackmax[oc[i]]=dv;
      noiset_feedbackav[oc[i]]+=dv;
      noiset_feedbackcount[oc[i]]++;

      if(M-noise_tfull[i]>0){
	double div=(M-noise_tfull[i])/M;
	b[i*2]*=div;
	b[i*2-1]*=div;
      }else{
	b[i*2]=0;
	b[i*2-1]=0;
      }
    }

    dv=fabs(b[block-1])/noise_tfull[block/2];
    if(noiset_feedbackmax[oc[block/2]]<dv)noiset_feedbackmax[oc[block/2]]=dv;
    noiset_feedbackav[oc[block/2]]+=dv;
    noiset_feedbackcount[oc[block/2]]++;

    if(b[i*2-1]<0){
      b[i*2-1]+=noise_tfull[block/2];
      if(b[i*2-1]>0)b[i*2-1]=0;
    }else{
      b[i*2-1]-=noise_tfull[block/2];
      if(b[i*2-1]<0)b[i*2-1]=0;
    }

  }else{
    pthread_mutex_unlock(&master_mutex);
  }
}

void eq_filter(double *b){
  int i;
  double dv;
  pthread_mutex_lock(&master_mutex);
  if(wc.eq_p){

    if(eq_dirty){
      refresh_thresh_array(eq_tfull,wc.eqt);
      eq_dirty=0;
    }
      
    pthread_mutex_unlock(&master_mutex);
    
    b[0]*=eq_tfull[0];
    dv=fabs(b[0]);
    if(eqt_feedbackmax[oc[0]]<dv)eqt_feedbackmax[oc[0]]=dv;
    eqt_feedbackav[oc[0]]+=dv;
    eqt_feedbackcount[oc[0]]++;
    
    for(i=1;i<block/2;i++){
      b[i*2]*=eq_tfull[i];
      b[i*2-1]*=eq_tfull[i];
      
      dv=sqrt(b[i*2]*b[i*2]+b[i*2-1]*b[i*2-1]);
      if(eqt_feedbackmax[oc[i]]<dv)eqt_feedbackmax[oc[i]]=dv;
      eqt_feedbackav[oc[i]]+=dv;
      eqt_feedbackcount[oc[i]]++;

    }

    b[block-1]*=eq_tfull[block/2-1];
    dv=fabs(b[block-1]);
    if(eqt_feedbackmax[oc[i]]<dv)eqt_feedbackmax[oc[i]]=dv;
    eqt_feedbackav[oc[i]]+=dv;
    eqt_feedbackcount[oc[i]]++;

  }else{
    pthread_mutex_unlock(&master_mutex);
  }
}

int aseek(off_t pos){
  int i;

  if(pos<0)pos=0;

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
  if(Bcursor<0 && cursor>=current_file_entry->end){
    pthread_mutex_unlock(&master_mutex);
    return -1; /* EOF */
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

    if(ret>=0){
      read_b+=ret;
      toread_b-=ret;
      cursor+=ret;
    }

    if(Bcursor!=-1 && cursor>=Bcursor){
      aseek(Acursor);
    }else if(cursor>=current_file_entry->end){
      if(current_file_entry_number+1<file_entries){
	current_file_entry_number++;
	current_file_entry++;
	fseeko(current_file_entry->f,current_file_entry->data,SEEK_SET);
      }else{
	memset(readbuf+read_b,0,toread_b);
	read_b+=toread_b;
	toread_b=0;
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
  fseek(f,0,SEEK_SET);
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

/* playback must be halted to change blocksize. */
void *playback_thread(void *vfile){
  FILE *playback_fd=NULL;
  char *file=(char *)vfile;
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

  if(!strcmp(file,"stdout")){
    playback_fd=stdout;
  }else{
    playback_fd=fopen(file,"wb");
    if(!playback_fd){
      pthread_mutex_lock(&master_mutex);
      playback_active=0;
      playback_exit=0;
      pthread_mutex_unlock(&master_mutex);
      return NULL;
    }
  }

  if(!strncmp(file,"/dev/",5)){
    
    fd=fileno(playback_fd);
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
    WriteWav(playback_fd,ch,rate,outbytes*8,file_list[file_entries-1].end);
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
      dynamic_att(w);
      
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

    fwrite(audiobuf,1,audiobufsize,playback_fd);

  }

  pthread_mutex_lock(&master_mutex);
  fclose(playback_fd);
  playback_active=0;
  playback_exit=0;
  drft_clear(&fft);
  pthread_mutex_unlock(&master_mutex);
  write(ttypipe[1],"",1);
  return(NULL);
}

void _analysis(char *base,int i,double *v,int n,int dB){
  int j;
  FILE *of;
  char buffer[80];

  sprintf(buffer,"%s_%d.m",base,i);
  of=fopen(buffer,"w");
  
  if(!of)perror("failed to open data dump file");
  
  for(j=0;j<n;j++){
    fprintf(of,"%f ",(double)toOC((j+1)*22050./n)*2);
    
    if(dB){
      float val;
      if(v[j]==0.)
	val=-140.;
      else
	val=todB(v[j]);
      fprintf(of,"%f\n",val);
    }else{
      fprintf(of,"%f\n",v[j]);
    }
  }
  fclose(of);
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
	
	fprintf(f,"NAME:%s\n",configlist[i].name);
      }
    }
    
    fprintf(f,"CONFIG_ACTIVE:%ld\n",configactive);
    
    fprintf(f,"A:%ld\n",A);
    fprintf(f,"B:%ld\n",B);
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
      
      confparse_s(buffer,"NAME",configlist[i].name,sizeof(configlist[i].name));
    }

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

  /* parse command line and open all the input files */
  file_list=calloc(argc-1,sizeof(file_entry));
  file_entries=argc-1;
  for(i=1;i<argc;i++){
    FILE *f=fopen(argv[i],"rb");
    if(f){
      unsigned char buffer[81];
      off_t filelength;
      int datap=0;
      int fmtp=0;
      file_list[i-1].name=strdup(argv[i]);
      file_list[i-1].f=f;
      
      /* parse header (well, sort of) and get file size */
      fseek(f,0,SEEK_END);
      filelength=ftello(f);
      fseek(f,0,SEEK_SET);

      fread(buffer,1,12,f);
      if(strncmp(buffer,"RIFF",4) || strncmp(buffer+8,"WAVE",4)){
	fprintf(stderr,"%s: Not a WAVE file.\n",argv[i]);
	exit(1);
      }

      while(fread(buffer,1,8,f)==8){
	int chunklen=buffer[4]|(buffer[5]<<8)|(buffer[6]<<16)|(buffer[7]<<24);

	if(!strncmp(buffer,"fmt ",4)){
	  int ltype;
	  int lch;
	  int lrate;
	  int lbits;

	  if(chunklen>80){
	    fprintf(stderr,"%s: WAVE file fmt chunk too large to parse.\n",argv[i]);
	    exit(1);
	  }
	  fread(buffer,1,chunklen,f);
	  
	  ltype=buffer[0] |(buffer[1]<<8);
	  lch=  buffer[2] |(buffer[3]<<8);
	  lrate=buffer[4] |(buffer[5]<<8)|(buffer[6]<<16)|(buffer[7]<<24);
	  lbits=buffer[14]|(buffer[15]<<8);

	  if(ltype!=1){
	    fprintf(stderr,"%s: WAVE file not PCM.\n",argv[i]);
	    exit(1);
	  }

	  if(i==1){
	    ch=lch;
	    rate=lrate;
	    inbytes=lbits/8;
	    if(inbytes>1)signp=1;
	  }else{
	    if(ch!=lch){
	      fprintf(stderr,"%s: WAVE files must all have same number of channels.\n",argv[i]);
	      exit(1);
	    }
	    if(rate!=lrate){
	      fprintf(stderr,"%s: WAVE files must all be same sampling rate.\n",argv[i]);
	      exit(1);
	    }
	    if(inbytes!=lbits/8){
	      fprintf(stderr,"%s: WAVE files must all be same sample width.\n",argv[i]);
	      exit(1);
	    }
	  }
	  fmtp=1;
	} else if(!strncmp(buffer,"data",4)){
	  off_t pos=ftello(f);
	  if(!fmtp){
	    fprintf(stderr,"%s: WAVE fmt chunk must preceed data chunk.\n",argv[i]);
	    exit(1);
	  }
	  datap=1;
	  
	  filelength=(filelength-pos)/(ch*inbytes)*(ch*inbytes)+pos;
	  
	  if(chunklen+pos<=filelength){
	    file_list[i-1].begin=total;
	    total=file_list[i-1].end=total+chunklen;
	    fprintf(stderr,"%s: Using declared file size.\n",argv[i]);
	  }else{
	    file_list[i-1].begin=total;
	    total=file_list[i-1].end=total+filelength-pos;
	    fprintf(stderr,"%s: Using actual file size.\n",argv[i]);
	  }
	  file_list[i-1].data=ftello(f);
	  break;
	} else {
	  fprintf(stderr,"%s: Unknown chunk type %c%c%c%c; skipping.\n",argv[i],
		  buffer[0],buffer[1],buffer[2],buffer[3]);
	  for(j=0;j<chunklen;j++)
	    if(fgetc(f)==EOF)break;
	}
      }

      if(!datap){
	fprintf(stderr,"%s: WAVE file has no data chunk.\n",argv[i]);
	exit(1);
      }
      
    }else{
      fprintf(stderr,"%s: Unable to open file.\n",argv[i]);
      exit(1);
    }
  }

  /* load config */
  {
    configfd=open(".postfishrc",O_RDWR|O_CREAT,0666);
    if(configfd>=0)load_settings(configfd);
  }

  /* are we running in batch mode? */
  if(!strcmp(argv[0],"postfish-batch")){
    playback_thread("stdout");
    exit(0);
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
    int ysize=10+BANDS;
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
    mvaddstr(0, 2, " Postfish Filter build 20021120.0 ");
    mvaddstr(LINES-1, 2, 
	     " [<]<<   [,]<   [Spc] Play/Pause   [Bksp] Stop   [.]>   [>]>>   [p] Process ");

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
    update_play();
    update_static_0();

    refresh();
    
    signal(SIGINT,SIG_IGN);

    while(1){
      int c=form_handle_char(&editf,getch());
      if(c=='q')break;
      switch(c){
      case '<':
	pthread_mutex_lock(&master_mutex);
	aseek(cursor-rate*ch*inbytes*60);
	pthread_mutex_unlock(&master_mutex);
	update_ui();
	break;
      case ',':
	pthread_mutex_lock(&master_mutex);
	aseek(cursor-rate*ch*inbytes*5);
	pthread_mutex_unlock(&master_mutex);
	update_ui();
	break;
      case '>':
	pthread_mutex_lock(&master_mutex);
	aseek(cursor+rate*ch*inbytes*60);
	pthread_mutex_unlock(&master_mutex);
	update_ui();
	break;
      case '.':
	pthread_mutex_lock(&master_mutex);
	aseek(cursor+rate*ch*inbytes*5);
	pthread_mutex_unlock(&master_mutex);
	update_ui();
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
	wc.dyn_p=!wc.dyn_p;
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
	    update_play();
	  }else{
	    pthread_mutex_lock(&master_mutex);
	    TX[num]=cursor_to_time(cursor);
	    pthread_mutex_unlock(&master_mutex);
	    update_0();
	  }
	}
	update_ui();
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
	  }
	  update_a();
	}else{
	  Acursor=time_to_cursor(A);
	  aseek(Acursor);
	  pthread_mutex_unlock(&master_mutex);
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
	cursor=Acursor;
	pthread_mutex_unlock(&master_mutex);
	update_play();
	break;
      case ' ':
	pthread_mutex_lock(&master_mutex);
	if(!playback_active){
	  playback_active=1;
	  pthread_mutex_unlock(&master_mutex);
	  update_play();
	  pthread_create(&playback_thread_id,NULL,&playback_thread,"/dev/dsp");
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
	  update_play();
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
	  update_ui();
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
	  update_ui();
	  form_redraw(&editf);
	  form_redraw(&noneditf);
	  update_D();
	}
	break;
      case 0:
	update_play();
	update_ui();
	break;
      default:
	update_ui();
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
