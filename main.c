/*
 *
 *  postfish
 *    
 *      Copyright (C) 2002-2003 Monty
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
#include <fcntl.h>
#include <sys/soundcard.h>
#include <sys/ioctl.h>

#include "mainpanel.h"

static int outfileno=-1;
sig_atomic_t loop_flag=1;
static int inbytes=0;
static int outbytes=2;
static int rate=0;
sig_atomic_t ch=0;
static int signp=0;

/* working space */
typedef struct time_linkage {
  int samples;
  int channels;
  double **data;
} time_linkage;

static off_t Acursor=0;
static off_t Bcursor=-1;
static long  T=-1;
static off_t cursor=0;

pthread_mutex_t master_mutex=PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

sig_atomic_t primed=0;
sig_atomic_t playback_active=0;
sig_atomic_t playback_exit=0;
sig_atomic_t loop_active;

int eventpipe[2];

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
  int bigendianp=0;
  double scale;
  int last=0;
  off_t count=0;

  pthread_mutex_lock(&master_mutex);
  //block=wc.block_a;
  //scale=2./block;
  pthread_mutex_unlock(&master_mutex);

  if(outfileno==-1){
    playback_fd=fopen("/dev/dsp","wb");
  }else{
    playback_fd=fdopen(dup(outfileno),"wb");
  }
  if(!playback_fd){
    playback_active=0;
    playback_exit=0;
    return NULL;
  }

  /* is this file a block device? */
  if(isachr(playback_fd)){
    int fragment=0x7fff0008;

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
  aseek(cursor);
  primed=0;

  while(1){
    if(playback_exit)break;

    /* get data */
    if(aread(buf))break;


    /* processing goes here */


    /* Back to L/R if stereo */
    
    if(ch==2){
      for(i=0;i<block;i++){
	double L=(buf[0][i]+buf[1][i])*.5;
	double R=(buf[0][i]-buf[1][i])*.5;
	buf[0][i]=L;
	buf[1][i]=R;
      }
    }

    /* final limiting */

    for(k=0,i=0;i<block/2;i++){
      for(j=0;j<ch;j++){
	int val=rint(buf[j][i]*32767.);
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
	write(eventpipe[1],"",1);
      last=foo;
    }

    count+=fwrite(audiobuf,1,audiobufsize,playback_fd);

  }

  if(isachr(playback_fd)){
    fd=fileno(playback_fd);
    ret=ioctl(fd,SNDCTL_DSP_RESET);
  }else{
    WriteWav(playback_fd,ch,rate,outbytes*8,count);
  }

  fclose(playback_fd);
  playback_active=0;
  playback_exit=0;
  write(eventpipe[1],"",1);
  return(NULL);
}

int main(int argc, char **argv){
  off_t total=0;
  int i,j;
  int configfd=-1;
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
#if 0
  {
    configfd=open(".postfishrc",O_RDWR|O_CREAT,0666);
    if(configfd>=0)load_settings(configfd);
  }
#endif

  /* set up the hack for interthread gtk event triggering through
   input subversion */
  if(pipe(eventpipe)){
    fprintf(stderr,"Unable to open event pipe:\n"
            "  %s\n",strerror(errno));
    
    exit(1);
  }

  aseek(0);

  mainpanel_go(argc,argv);

  playback_exit=1;

  while(1){
    if(playback_active){
      sched_yield();
    }else
      break;
  }

  //save_settings(configfd);
  if(configfd>=0)close(configfd);
  return(0);
}





