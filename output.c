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
#include "feedback.h"
#include "input.h"
#include "output.h"
#include "declip.h"
#include "eq.h"
#include "multicompand.h"
#include "singlecomp.h"
#include "suppress.h"
#include "limit.h"
#include "mute.h"

extern int input_size;
sig_atomic_t playback_active=0;
sig_atomic_t playback_exit=0;
sig_atomic_t playback_seeking=0;

void output_reset(void){
  /* empty feedback queues */
  while(pull_output_feedback(NULL,NULL));
  return;
}

void pipeline_reset(){
  int flags=fcntl(eventpipe[0],F_GETFL);
  char buf[1];
  /* drain the event pipe */
  if(fcntl(eventpipe[0],F_SETFL,O_NONBLOCK))
    fprintf(stderr,"Unable to set O_NONBLOCK on event pipe.\n");
  while(read(eventpipe[0],buf,1)>0);
  fcntl(eventpipe[0],F_SETFL,flags);

  input_reset();  /* clear any persistent lapping state */
  declip_reset();  /* clear any persistent lapping state */
  eq_reset();      /* clear any persistent lapping state */
  multicompand_reset(); /* clear any persistent lapping state */
  singlecomp_reset(); /* clear any persistent lapping state */
  suppress_reset(); /* clear any persistent lapping state */
  limit_reset(); /* clear any persistent lapping state */
  output_reset(); /* clear any persistent lapping state */
}

typedef struct output_feedback{
  feedback_generic parent_class;
  float *rms;
  float *peak;
} output_feedback;

static feedback_generic_pool feedpool;

static feedback_generic *new_output_feedback(void){
  output_feedback *ret=malloc(sizeof(*ret));
  ret->rms=malloc((input_ch+2)*sizeof(*ret->rms));
  ret->peak=malloc((input_ch+2)*sizeof(*ret->peak));
  return (feedback_generic *)ret;
}

static void push_output_feedback(float *peak,float *rms){
  int n=input_ch+2;
  output_feedback *f=(output_feedback *)
    feedback_new(&feedpool,new_output_feedback);
  
  memcpy(f->rms,rms,n*sizeof(*rms));
  memcpy(f->peak,peak,n*sizeof(*peak));
  feedback_push(&feedpool,(feedback_generic *)f);
}

int pull_output_feedback(float *peak,float *rms){
  output_feedback *f=(output_feedback *)feedback_pull(&feedpool);
  int n=input_ch+2;
  if(!f)return 0;
  if(rms)memcpy(rms,f->rms,sizeof(*rms)*n);
  if(peak)memcpy(peak,f->peak,sizeof(*peak)*n);
  feedback_old(&feedpool,(feedback_generic *)f);
  return 1;
}

int output_feedback_deep(void){
  return feedback_deep(&feedpool);
}

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

static int ilog(long x){
  int ret=-1;

  while(x>0){
    x>>=1;
    ret++;
  }
  return ret;
}

static int outbytes;
static FILE *playback_startup(int outfileno, int ch, int r, int *bep){
  FILE *playback_fd=NULL;
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
    long bytesperframe=input_size*ch*2;
    int fd=fileno(playback_fd);
    int format=AFMT_S16_NE;
    int fraglog=ilog(bytesperframe);
    int fragment=0x00040000|fraglog;
    outbytes=2;

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
    outbytes=3;
    WriteWav(playback_fd,ch,r,24,-1);
    *bep=0;
  }

  return playback_fd;
}

/* playback must be halted to change blocksize. */
void *playback_thread(void *dummy){
  int audiobufsize=0,i,j,k;
  unsigned char *audiobuf=NULL;
  int bigendianp=(AFMT_S16_NE==AFMT_S16_BE?1:0);
  FILE *playback_fd=NULL;
  int setupp=0;
  time_linkage *link;
  int result;
  off_t count=0;

  int ch=-1;
  long rate=-1;

  /* for output feedback */
  float *rms=alloca(sizeof(*rms)*(input_ch+2));
  float *peak=alloca(sizeof(*peak)*(input_ch+2));

  while(1){
    if(playback_seeking){
      pipeline_reset();
      playback_seeking=0;
    }

    if(playback_exit)break;

    /* get data */
    link=input_read();
    result=link->samples;
    link=mute_read(link);
    result|=link->samples;

    link=declip_read(link);
    result|=link->samples;





    link=multicompand_read_master(link);
    result|=link->samples;
    link=singlecomp_read(link);
    result|=link->samples;
    link=suppress_read(link);
    result|=link->samples;
    link=eq_read(link);
    result|=link->samples;
    
    if(!result)break;
    /************/
    
    
    /* master att */
    if(link->samples>0){
      float scale=fromdB(master_att/10.);
      for(i=0;i<link->samples;i++)
	for(j=0;j<link->channels;j++)
	  link->data[j][i]*=scale;
    }    


    /* the limiter is single-block zero additional latency */
    link=limit_read(link);

    /************/

    if(link->samples>0){

      memset(rms,0,sizeof(*rms)*(input_ch+2));
      memset(peak,0,sizeof(*peak)*(input_ch+2));
      ch=link->channels;
      rate=link->rate;
      
      /* lazy playbak setup; we couldn't do it until we had rate and
	 channel information from the pipeline */
      if(!setupp){
	playback_fd=playback_startup(outfileno,ch,rate,&bigendianp);
	if(!playback_fd){
	  playback_active=0;
	  playback_exit=0;
	  return NULL;
	}
	setupp=1;
      }
      
      if(audiobufsize<link->channels*link->samples*outbytes){
	audiobufsize=link->channels*link->samples*outbytes;
	if(audiobuf)
	  audiobuf=realloc(audiobuf,sizeof(*audiobuf)*audiobufsize);
	else
	  audiobuf=malloc(sizeof(*audiobuf)*audiobufsize);
      }
      
      /* final limiting and conversion */

      for(k=0,i=0;i<link->samples;i++){
	float mean=0.;
	float divrms=0.;
	
	for(j=0;j<link->channels;j++){
	  float dval=link->data[j][i];

	  switch(outbytes){
	  case 3:
	    {
	      int32_t val=rint(dval*8388608.);
	      if(val>8388607)val=8388607;
	      if(val<-8388608)val=-8388608;
	      
	      if(bigendianp){
		audiobuf[k++]=val>>16;
		audiobuf[k++]=val>>8;
		audiobuf[k++]=val;
	      }else{
		audiobuf[k++]=val;
		audiobuf[k++]=val>>8;
		audiobuf[k++]=val>>16;
	      }
	    }
	    break;
	  case 2:
	    {
	      int32_t val=rint(dval*32768.);
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
	    break;
	  }	  

	  if(fabs(dval)>peak[j])peak[j]=fabs(dval);
	  rms[j]+= dval*dval;
	  mean+=dval;
	  
	}
	
	/* mean */
	mean/=j;
	if(fabs(mean)>peak[input_ch])peak[input_ch]=fabs(mean);
	rms[input_ch]+= mean*mean;
	
	/* div */
	for(j=0;j<link->channels;j++){
	  float dval=mean-link->data[j][i];
	  if(fabs(dval)>peak[input_ch+1])peak[input_ch+1]=fabs(dval);
	  divrms+=dval*dval;
	}
	rms[input_ch+1]+=divrms/link->channels;
	
      }

      for(j=0;j<input_ch+2;j++){
	rms[j]/=link->samples;
	rms[j]=sqrt(rms[j]);
      }
      
      count+=fwrite(audiobuf,1,link->channels*link->samples*outbytes,playback_fd);
      
      /* inform Lord Vader his shuttle is ready */
      push_output_feedback(peak,rms);

      write(eventpipe[1],"",1);
      
    }

  }

  if(playback_fd){
    if(isachr(playback_fd)){
      int fd=fileno(playback_fd);
      ioctl(fd,SNDCTL_DSP_RESET);
    }else{
      if(ch>-1)
	WriteWav(playback_fd,ch,rate,24,count);
    } 
    fclose(playback_fd);
  }
  playback_active=0;
  playback_exit=0;
  if(audiobuf)free(audiobuf);
  write(eventpipe[1],"",1);
  return(NULL);
}

void output_halt_playback(void){
  if(playback_active){
    playback_exit=1;
    
    while(1){
      if(playback_active){
	sched_yield();
      }else
	break;
    }
  }
}


