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
#include "feedback.h"
#include "input.h"

static off_t        Acursor=0;
static off_t        Bcursor=-1;
static off_t        cursor=0;

sig_atomic_t loop_active;
int seekable;

int input_rate;
int input_ch;
int inbytes;
static int signp;
int input_size;

typedef struct {
  FILE *f;
  
  off_t begin;
  off_t end;
  off_t data;

} file_entry;

typedef struct input_feedback{
  feedback_generic parent_class;
  off_t   cursor;
  float *rms;
  float *peak;
} input_feedback;

static feedback_generic_pool feedpool;

static file_entry *file_list=NULL;
static int file_entries=0;
static int current_file_entry_number=-1;
static file_entry *current_file_entry=NULL;
static time_linkage out;

void input_Acursor_set(off_t c){
  pthread_mutex_lock(&master_mutex);
  Acursor=c;
  pthread_mutex_unlock(&master_mutex);
}

void input_Bcursor_set(off_t c){
  pthread_mutex_lock(&master_mutex);
  Bcursor=c;
  pthread_mutex_unlock(&master_mutex);
}

off_t input_time_to_cursor(const char *t){
  char temp[14];
  char *c;

  int hd;
  int s;
  int m;
  int h;

  strncpy(temp,t,14);

  /* hundredths */
  c=strchr(temp,'.');
  if(c){
    *c=0;
    hd=atoi(c+1);
    if(hd>99)hd=99;
    if(hd<0)hd=0;
  }else
    hd=0;

  /* seconds */
  c=strrchr(temp,':');
  if(c){
    *c=0;
    s=atoi(c+1);
    if(s>59)s=59;
    if(s<0)s=0;
  }else{
    s=atoi(temp);
    *temp=0;
  }

  /* minutes */
  c=strrchr(temp,':');
  if(c){
    *c=0;
    m=atoi(c+1);
    if(m>59)m=59;
    if(m<0)m=0;
  }else{
    m=atoi(temp);
    *temp=0;
  }

  /* hours */
  h=atoi(temp);
  if(h>9999)h=9999;
  if(h<0)h=0;

  return ((off_t)hd + (off_t)s*100 + (off_t)m*60*100 + (off_t)h*60*60*100) *
    input_rate / 100 * inbytes * input_ch;
}

void time_fix(char *buffer){
  if(buffer[0]=='0')buffer[0]=' ';
  if(!strncmp(buffer," 0",2))buffer[1]=' ';
  if(!strncmp(buffer,"  0",3))buffer[2]=' ';
  if(!strncmp(buffer,"   0",4))buffer[3]=' ';
  if(!strncmp(buffer,"    :0",6))buffer[5]=' ';
  if(!strncmp(buffer,"    : 0",7))buffer[6]=' ';
  
  if(buffer[0]!=' ' && buffer[1]==' ')buffer[1]='0';
  if(buffer[1]!=' ' && buffer[2]==' ')buffer[2]='0';
  if(buffer[2]!=' ' && buffer[3]==' ')buffer[3]='0';
  if(buffer[3]!=' ' && buffer[5]==' ')buffer[5]='0';
  if(buffer[5]!=' ' && buffer[6]==' ')buffer[6]='0';
}

void input_cursor_to_time(off_t cursor,char *t){
  int h,m,s,hd;
  cursor/=input_ch*inbytes;

  h=cursor/60/60/input_rate;
  cursor%=(off_t)60*60*input_rate;
  m=cursor/60/input_rate;
  cursor%=(off_t)60*input_rate;
  s=cursor/input_rate;
  hd=cursor%input_rate*100/input_rate;
  if(h>9999)h=9999;

  sprintf(t,"%04d:%02d:%02d.%02d",h,m,s,hd);
  time_fix(t);
}

int input_load(int n,char *list[]){
  char *fname="stdin";
  int stdinp=0,i,j,ch=0,rate=0;
  off_t total=0;

  if(n==0){
    /* look at stdin... is it a file, pipe, tty...? */
    if(isatty(STDIN_FILENO)){
      fprintf(stderr,
	      "Postfish requires input either as a list of contiguous WAV\n"
	      "files on the command line, or WAV data piped|redirected to\n"
	      "stdin.\n");
      return 1;
    }
    stdinp=1;    /* file coming in via stdin */
    file_entries=1;
  }else
    file_entries=n;

  file_list=calloc(file_entries,sizeof(file_entry));
  for(i=0;i<file_entries;i++){
    FILE *f;
    
    if(stdinp){
      int newfd=dup(STDIN_FILENO);
      f=fdopen(newfd,"rb");
    }else{
      f=fopen(list[i],"rb");
      fname=list[i];
    }

    if(f){
      unsigned char buffer[81];
      off_t filelength;
      int datap=0;
      int fmtp=0;
      file_list[i].f=f;
      
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
	return 1;
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
	    return 1;
	  }
	  fread(buffer,1,chunklen,f);
	  
	  ltype=buffer[0] |(buffer[1]<<8);
	  lch=  buffer[2] |(buffer[3]<<8);
	  lrate=buffer[4] |(buffer[5]<<8)|(buffer[6]<<16)|(buffer[7]<<24);
	  lbits=buffer[14]|(buffer[15]<<8);

	  if(ltype!=1){
	    fprintf(stderr,"%s: WAVE file not PCM.\n",fname);
	    return 1;
	  }

	  if(i==0){
	    ch=lch;
	    rate=lrate;
	    inbytes=lbits/8;
	    if(inbytes>1)signp=1;
	  }else{
	    if(ch!=lch){
	      fprintf(stderr,"%s: WAVE files must all have same number of channels.\n",fname);
	      return 1;
	    }
	    if(rate!=lrate){
	      fprintf(stderr,"%s: WAVE files must all be same sampling rate.\n",fname);
	      return 1;
	    }
	    if(inbytes!=lbits/8){
	      fprintf(stderr,"%s: WAVE files must all be same sample width.\n",fname);
	      return 1;
	    }
	  }
	  fmtp=1;
	} else if(!strncmp(buffer,"data",4)){
	  off_t pos=ftello(f);
	  if(!fmtp){
	    fprintf(stderr,"%s: WAVE fmt chunk must preceed data chunk.\n",fname);
	    return 1;
	  }
	  datap=1;
	  
	  if(seekable)
	    filelength=(filelength-pos)/(ch*inbytes)*(ch*inbytes)+pos;

	  if(chunklen==0UL ||
	     chunklen==0x7fffffffUL || 
	     chunklen==0xffffffffUL){
	    file_list[i].begin=total;
	    total=file_list[i].end=0;
	    fprintf(stderr,"%s: Incomplete header; assuming stream.\n",fname);
	  }else if(filelength==-1 || chunklen+pos<=filelength){
	    file_list[i].begin=total;
	    total=file_list[i].end=total+chunklen;
	    fprintf(stderr,"%s: Using declared file size.\n",fname);
	  }else{
	    file_list[i].begin=total;
	    total=file_list[i].end=total+filelength-pos;
	    fprintf(stderr,"%s: Using actual file size.\n",fname);
	  }
	  file_list[i].data=ftello(f);
	  
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
	return 1;
      }
      
    }else{
      fprintf(stderr,"%s: Unable to open file.\n",fname);
      return 1;
    }
  }


  /* 192000: 8192

      96000: 4096
      88200: 4096
      64000: 4096

      48000: 2048
      44100: 2048
      32000: 1024

      22050: 1024
      16000: 1024

      11025:  512
       8000:  512

       4000:  256 */

  if(rate<6000){
    input_size=256;
  }else if(rate<15000){
    input_size=512;
  }else if(rate<25000){
    input_size=1024;
  }else if(rate<50000){
    input_size=2048;
  }else if(rate<100000){
    input_size=4096;
  }else
    input_size=8192;

  input_ch=out.channels=ch;
  input_rate=rate;
  out.data=malloc(sizeof(*out.data)*ch);
  for(i=0;i<ch;i++)
    out.data[i]=malloc(sizeof(*out.data[0])*input_size);
  
  return 0;
}

off_t input_seek(off_t pos){
  int i;

  if(pos<0)pos=0;
  if(!seekable){
    current_file_entry=file_list;
    current_file_entry_number=0;
    return -1;
  }

  for(i=0;i<file_entries;i++){
    current_file_entry=file_list+i;
    current_file_entry_number=i;
    if(current_file_entry->begin<=pos && current_file_entry->end>pos){
      fseeko(current_file_entry->f,
	     pos-current_file_entry->begin+current_file_entry->data,
	     SEEK_SET);
      pthread_mutex_lock(&master_mutex);
      cursor=pos;
      playback_seeking=1;
      pthread_mutex_unlock(&master_mutex);
      return cursor;
    }
  }
  i--;

  pos=current_file_entry->end;
  fseeko(current_file_entry->f,
	 pos-current_file_entry->begin+current_file_entry->data,
	 SEEK_SET);
  pthread_mutex_lock(&master_mutex);
  cursor=pos;
      playback_seeking=1;
  pthread_mutex_unlock(&master_mutex);
  return cursor;
}

off_t input_time_seek_rel(float s){
  off_t ret;
  pthread_mutex_lock(&master_mutex);
  ret=input_seek(cursor+input_rate*inbytes*input_ch*s);
  pthread_mutex_unlock(&master_mutex);
  return ret;
}

static feedback_generic *new_input_feedback(void){
  input_feedback *ret=malloc(sizeof(*ret));
  ret->rms=malloc((input_ch+2)*sizeof(*ret->rms));
  ret->peak=malloc((input_ch+2)*sizeof(*ret->peak));
  return (feedback_generic *)ret;
}

static void push_input_feedback(float *peak,float *rms, off_t cursor){
  int n=input_ch+2;
  input_feedback *f=(input_feedback *)
    feedback_new(&feedpool,new_input_feedback);
  f->cursor=cursor;
  memcpy(f->rms,rms,n*sizeof(*rms));
  memcpy(f->peak,peak,n*sizeof(*peak));
  feedback_push(&feedpool,(feedback_generic *)f);
}

int pull_input_feedback(float *peak,float *rms,off_t *cursor){
  input_feedback *f=(input_feedback *)feedback_pull(&feedpool);
  int n=input_ch+2;
  if(!f)return 0;
  if(rms)memcpy(rms,f->rms,sizeof(*rms)*n);
  if(peak)memcpy(peak,f->peak,sizeof(*peak)*n);
  if(cursor)*cursor=f->cursor;
  feedback_old(&feedpool,(feedback_generic *)f);
  return 1;
}

time_linkage *input_read(void){
  int read_b=0,i,j,k;
  int toread_b=input_size*out.channels*inbytes;
  unsigned char *readbuf;
  float *rms=alloca(sizeof(*rms)*(out.channels+2));
  float *peak=alloca(sizeof(*peak)*(out.channels+2));

  memset(rms,0,sizeof(*rms)*(out.channels+2));
  memset(peak,0,sizeof(*peak)*(out.channels+2));

  pthread_mutex_lock(&master_mutex);
  out.samples=0;

  /* the non-streaming case */
  if(!loop_active && 
     cursor>=current_file_entry->end &&
     current_file_entry->end!=-1){
    pthread_mutex_unlock(&master_mutex);
    goto tidy_up;
  }

  /* the streaming case */
  if(feof(current_file_entry->f) && 
     current_file_entry_number+1>=file_entries){
    pthread_mutex_unlock(&master_mutex);
    goto tidy_up;
  }
  pthread_mutex_unlock(&master_mutex);

  readbuf=alloca(toread_b);

  while(toread_b){
    off_t ret;
    off_t read_this_loop=current_file_entry->end-cursor;
    if(read_this_loop>toread_b)read_this_loop=toread_b;

    ret=fread(readbuf+read_b,1,read_this_loop,current_file_entry->f);

    pthread_mutex_lock(&master_mutex);

    if(ret>0){
      read_b+=ret;
      toread_b-=ret;
      cursor+=ret;
    }else{
      if(current_file_entry_number+1>=file_entries){

	/* end of file before full frame */
	memset(readbuf+read_b,0,toread_b);
	toread_b=0;

      }
    }

    if(loop_active && cursor>=Bcursor){
      pthread_mutex_unlock(&master_mutex);
      input_seek(Acursor);
    }else{
      if(cursor>=current_file_entry->end){
	pthread_mutex_unlock(&master_mutex);
	if(current_file_entry_number+1<file_entries){
	  current_file_entry_number++;
	  current_file_entry++;
	  fseeko(current_file_entry->f,current_file_entry->data,SEEK_SET);
	}
      }else
	pthread_mutex_unlock(&master_mutex);
    }
  }

  out.samples=read_b/out.channels/inbytes;
  
  k=0;
  for(i=0;i<out.samples;i++){
    float mean=0.;
    float divrms=0.;

    for(j=0;j<out.channels;j++){
      float dval;
      long val=0;
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
	dval=out.data[j][i]=val/2147483648.;
      else
	dval=out.data[j][i]=(val^0x80000000UL)/2147483648.;

      if(fabs(dval)>peak[j])peak[j]=fabs(dval);
      rms[j]+= dval*dval;
      mean+=dval;

      k+=inbytes;
    }

    /* mean */
    mean/=j;
    if(fabs(mean)>peak[j])peak[j]=fabs(mean);
    rms[j]+= mean*mean;

    /* div */
    for(j=0;j<out.channels;j++){
      float dval=mean-out.data[j][i];
      if(fabs(dval)>peak[out.channels+1])peak[out.channels+1]=fabs(dval);
      divrms+=dval*dval;
    }
    rms[out.channels+1]+=divrms/out.channels;
      
  }    
  
  for(j=0;j<out.channels+2;j++){
    rms[j]/=out.samples;
    rms[j]=sqrt(rms[j]);
  }

  push_input_feedback(peak,rms,cursor);

 tidy_up:
  {
    int tozero=input_size-out.samples;
    if(tozero)
      for(j=0;j<out.channels;j++)
	memset(out.data[j]+out.samples,0,sizeof(**out.data)*tozero);
  }

  return &out;
}

void input_reset(void){
  while(pull_input_feedback(NULL,NULL,NULL));
  return;
}



