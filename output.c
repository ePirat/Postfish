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

/* sound playback code is OSS-specific for now */
#include <linux/soundcard.h>
#include <sys/ioctl.h>
#include "postfish.h"
#include "input.h"

sig_atomic_t playback_active=0;
sig_atomic_t playback_exit=0;

static void PutNumLE(long num,FILE *f,int bytes){
  int i=0;
  while(bytes--){
    fputc((num>>(i<<3))&0xff,f);
    i++;
  }
}

static void WriteWav(FILE *f,long channels,long rate,long bits,long duration){
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

static int isachr(FILE *f){
  struct stat s;

  if(!fstat(fileno(f),&s))
    if(S_ISCHR(s.st_mode)) return 1;
  return 0;
}

static FILE *playback_startup(int outfileno, int ch, int r){
  FILE *playback_fd=NULL;
  int format=AFMT_S16_NE;
  int rate=r,channels=ch,ret;

  if(outfileno==-1){
    playback_fd=fopen("/dev/dsp","wb");
  }else{
    playback_fd=fdopen(dup(outfileno),"wb");
  }

  if(!playback_fd){
    fprintf(stderr,"Unable to open output for playback\n");
    return NULL;
  }

  /* is this file a block device? */
  if(isachr(playback_fd)){
    int fragment=0x7fff000d;
    int fd=fileno(playback_fd);

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
    ret=ioctl(fd,SNDCTL_DSP_SPEED,&rate);
    if(ret || r!=rate){
      fprintf(stderr,"Could not set %dHz playback\n",r);
      exit(1);
    }
  }else{
    WriteWav(playback_fd,ch,r,16,-1);
  }

  return playback_fd;
}

/* playback must be halted to change blocksize. */
void *playback_thread(void *dummy){
  int audiobufsize=8192,i,j,k;
  unsigned char *audiobuf=malloc(audiobufsize);
  int bigendianp=(AFMT_S16_NE==AFMT_S16_BE?1:0);
  FILE *playback_fd=NULL;
  int setupp=0;
  time_linkage *ret;
  off_t count=0;
  long last=-1;

  int ch=-1;
  long rate=-1;

  while(1){
    if(playback_exit)break;

    /* get data */
    if(!(ret=input_read()))break;

    /************/




    /************/

    if(ret && ret->samples>0){
      ch=ret->channels;
      rate=ret->rate;

      /* lazy playbak setup; we couldn't do it until we had rate and
	 channel information from the pipeline */
      if(!setupp){
	playback_fd=playback_startup(outfileno,ch,rate);
	if(!playback_fd){
	  playback_active=0;
	  playback_exit=0;
	  return NULL;
	}
	setupp=1;
      }

      if(audiobufsize<ret->channels*ret->samples*2){
	audiobufsize=ret->channels*ret->samples*2;
	audiobuf=realloc(audiobuf,sizeof(*audiobuf)*audiobufsize);
      }
      
      /* final limiting and conversion */
      
      for(k=0,i=0;i<ret->samples;i++){
	for(j=0;j<ret->channels;j++){
	  int val=rint(ret->data[j][i]*32767.);
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
  }

  if(isachr(playback_fd)){
    int fd=fileno(playback_fd);
    ioctl(fd,SNDCTL_DSP_RESET);
  }else{
    if(ch>-1)
      WriteWav(playback_fd,ch,rate,16,count);
  }
  
  fclose(playback_fd);
  playback_active=0;
  playback_exit=0;
  if(audiobuf)free(audiobuf);
  write(eventpipe[1],"",1);
  return(NULL);
}

