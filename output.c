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
#include "deverb.h"
#include "limit.h"
#include "mute.h"
#include "mix.h"
#include "reverb.h"

output_settings outset;

extern int input_size;
extern int input_rate;
sig_atomic_t playback_active=0;
sig_atomic_t playback_exit=0;
sig_atomic_t playback_seeking=0;
sig_atomic_t master_att;

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
  deverb_reset(); /* clear any persistent lapping state */
  limit_reset(); /* clear any persistent lapping state */
  output_reset(); /* clear any persistent lapping state */
  mix_reset();
  plate_reset();
}

typedef struct output_feedback{
  feedback_generic parent_class;
  float *rms;
  float *peak;
} output_feedback;

static feedback_generic_pool feedpool;

static feedback_generic *new_output_feedback(void){
  output_feedback *ret=malloc(sizeof(*ret));
  ret->rms=malloc(OUTPUT_CHANNELS*sizeof(*ret->rms));
  ret->peak=malloc(OUTPUT_CHANNELS*sizeof(*ret->peak));
  return (feedback_generic *)ret;
}

static void push_output_feedback(float *peak,float *rms){
  output_feedback *f=(output_feedback *)
    feedback_new(&feedpool,new_output_feedback);
  
  memcpy(f->rms,rms,OUTPUT_CHANNELS*sizeof(*rms));
  memcpy(f->peak,peak,OUTPUT_CHANNELS*sizeof(*peak));
  feedback_push(&feedpool,(feedback_generic *)f);
}

int pull_output_feedback(float *peak,float *rms){
  output_feedback *f=(output_feedback *)feedback_pull(&feedpool);
  if(!f)return 0;
  if(rms)memcpy(rms,f->rms,sizeof(*rms)*OUTPUT_CHANNELS);
  if(peak)memcpy(peak,f->peak,sizeof(*peak)*OUTPUT_CHANNELS);
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

static void PutNumBE(long num,FILE *f,int bytes){
  switch(bytes){
  case 4:
    fputc((num>>24)&0xff,f);
  case 3:
    fputc((num>>16)&0xff,f);
  case 2:
    fputc((num>>8)&0xff,f);
  case 1:
    fputc(num&0xff,f);
  }
}

/* Borrowed from sox */
# define FloatToUnsigned(f) ((u_int32_t)(((int32_t)(f - 2147483648.0)) + 2147483647L) + 1)
static void ConvertToIeeeExtended(double num, char *bytes){
  int    sign;
  int expon;
  double fMant, fsMant;
  u_int32_t hiMant, loMant;
  
  if (num < 0) {
    sign = 0x8000;
    num *= -1;
  } else {
    sign = 0;
  }
  
  if (num == 0) {
    expon = 0; hiMant = 0; loMant = 0;
  }
  else {
    fMant = frexp(num, &expon);
    if ((expon > 16384) || !(fMant < 1)) {    /* Infinity or NaN */
      expon = sign|0x7FFF; hiMant = 0; loMant = 0; /* infinity */
    }
    else {    /* Finite */
      expon += 16382;
      if (expon < 0) {    /* denormalized */
	fMant = ldexp(fMant, expon);
	expon = 0;
      }
      expon |= sign;
      fMant = ldexp(fMant, 32);          
      fsMant = floor(fMant); 
      hiMant = FloatToUnsigned(fsMant);
      fMant = ldexp(fMant - fsMant, 32); 
      fsMant = floor(fMant); 
      loMant = FloatToUnsigned(fsMant);
    }
  }
  
  bytes[0] = expon >> 8;
  bytes[1] = expon;
  bytes[2] = hiMant >> 24;
  bytes[3] = hiMant >> 16;
  bytes[4] = hiMant >> 8;
  bytes[5] = hiMant;
  bytes[6] = loMant >> 24;
  bytes[7] = loMant >> 16;
  bytes[8] = loMant >> 8;
  bytes[9] = loMant;
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

void WriteAifc(FILE *f,long channels,long rate,long bits,long duration){
  int bytes=(bits+7)/8;
  long size=duration+86;
  long frames=duration/bytes/channels;
  char ieee[10];

  if(ftell(f)>0)
    if(fseek(f,0,SEEK_SET))
      return;
  
  /* Again, quick and dirty */
  
  fprintf(f,"FORM"); 
  if(duration>0)
    PutNumBE(size-8,f,4);
  else
    PutNumBE(0,f,4);
  fprintf(f,"AIFC");       
  fprintf(f,"FVER");       
  PutNumBE(4,f,4);
  PutNumBE(2726318400UL,f,4);

  fprintf(f,"COMM");
  PutNumBE(38,f,4);
  PutNumBE(channels,f,2);
  if(duration>0)
    PutNumBE(frames,f,4);
  else
    PutNumBE(-1,f,4);

  PutNumBE(bits,f,2);
  ConvertToIeeeExtended(rate,ieee);
  fwrite(ieee,1,10,f);

  fprintf(f,"NONE");
  PutNumBE(14,f,1);
  fprintf(f,"not compressed");
  PutNumBE(0,f,1);

  fprintf(f,"SSND");
  if(duration>0)
    PutNumBE(duration+8,f,4);
  else
    PutNumBE(0,f,4);

  PutNumBE(0,f,4);
  PutNumBE(0,f,4);

}

static int isachr(int f){
  struct stat s;

  if(!fstat(f,&s))
    if(S_ISCHR(s.st_mode)) return 1;
  return 0;
}

static int isareg(int f){
  struct stat s;

  if(!fstat(f,&s))
    if(S_ISREG(s.st_mode)) return 1;
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

static int is_oss(int f){
  struct stat s;

  if(isachr(f))
    if(!fstat(f,&s)){
      int major=(int)(s.st_rdev>>8)&0xff;
      int minor=(int)(s.st_rdev&0xff);

      // is this a Linux OSS audio device (Major 14) ?
      if(major==14 && ((minor&0xf)==3 || (minor&0xf)==4))return 1;
    }

  return 0;
}

static int is_alsa(int f){
  struct stat s;

  if(isachr(f))
    if(!fstat(f,&s)){
      int type=(int)(s.st_rdev>>8);

      // is this a Linux ALSA audio device (Major 116) ?
      if(type==116)return 1;
    }

  return 0;
}

static int isaudio(int outfileno){
  if(is_oss(outfileno))return 1;
  if(is_alsa(outfileno))return 2;
  return 0;
}

int output_stdout_available=0;
int output_stdout_device=0;    /* 0== file, 1==OSS, 2==ALSA */
int output_monitor_available=0;

int output_probe_stdout(int outfileno){
  int ret;

  if(isatty(outfileno)){
    /* stdout is the terminal; disable stdout */
    output_stdout_available=0;

  }else if (isareg(outfileno)){
    /* stdout is a regular file */
    output_stdout_available=1;
    output_stdout_device=0;

  }else if((ret==isaudio(outfileno))){
    /* stdout is an audio device */

    output_stdout_available=1;
    output_stdout_device=ret;

    if(ret==2){
      fprintf(stderr,
	      "\nStdout is pointing to an ALSA sound device;\n"
	      "Postfish does not yet support ALSA playback.\n\n");
      exit(1);
    }
  }else{
    /* God only knows what stdout is.  It might be /dev/null or some other.... treat it similar to file */

    output_stdout_available=1;
    output_stdout_device=0;

  }

  return 0;
}

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
output_monitor_entry *monitor_list;
int monitor_entries;

static char *audio_dev_type[]={"file","OSS"};

/* look for sound devices that actually exist */

static void add_monitor(char *file, char *name,int type){
  monitor_entries++;
  if(monitor_list){
    monitor_list=realloc(monitor_list,
			 monitor_entries*sizeof(*monitor_list));
  }else
    monitor_list=malloc(monitor_entries*sizeof(*monitor_list));
  
  monitor_list[monitor_entries-1].file=strdup(file);
  monitor_list[monitor_entries-1].name=strdup(name);
  monitor_list[monitor_entries-1].type=type;
  
  fprintf(stderr,"Found an output device (type %s): %s\n",
	  audio_dev_type[type],file);
}

static void output_probe_monitor_OSS(){
  /* open /dev and search of major 14 */
  DIR *d=opendir("/dev");
  struct dirent *de;
  
  if(d==NULL){
    fprintf(stderr,"Odd; could not open /dev to look for audio devices.\n");
    return;
  }
  
  while((de=readdir(d))){
    struct stat s;
    char buf[PATH_MAX];
    snprintf(buf,PATH_MAX,"/dev/%s",de->d_name);
    if(!stat(buf,&s)){
      
      int major=(int)(s.st_rdev>>8)&0xff;
      int minor=(int)(s.st_rdev&0xff);
      
      // is this a Linux OSS dsp audio device (Major 14, minor 3,19,35...) ?
      if(major==14 && (minor&0xf)==3){
	int f=open(buf,O_RDWR|O_NONBLOCK);
	if(f!=-1){
	  add_monitor(buf,de->d_name,1);
	  close(f);
	}
      }
    }
  }
	
  closedir(d);
}

static int moncomp(const void *a, const void *b){
  output_monitor_entry *ma=(output_monitor_entry *)a;
  output_monitor_entry *mb=(output_monitor_entry *)b;
  return(strcmp(ma->name,mb->name));
}

int output_probe_monitor(void ){
  if(output_stdout_device!=0){
    output_monitor_available=0;
    return 0;
  }
  
  output_probe_monitor_OSS();
  
  if(monitor_entries>0){
    output_monitor_available=1;

    /* sort the entries alphabetically by name */
    qsort(monitor_list, monitor_entries, sizeof(*monitor_list), moncomp);
  }

  return 0;
}

#ifndef AFMT_S24_LE
#       define AFMT_S24_LE              0x00000800      
#       define AFMT_S24_BE              0x00001000      
#       define AFMT_U24_LE              0x00002000      
#       define AFMT_U24_BE              0x00004000
#endif

static int OSS_playback_startup(FILE *f, int rate, int *ch, int *endian, int *bits,int *signp){
  int fd=fileno(f);
  int downgrade=0;
  int failflag=1;
  int local_ch=*ch;
  int local_bits=*bits;

  *endian=(AFMT_S16_NE==AFMT_S16_BE?1:0);
  
  /* try to configure requested playback.  If it fails, keep dropping back
     until one works */
  while(local_ch || local_bits>16){
    int format;
    
    switch(local_bits){
    case 8:
      format=AFMT_U8;
      *signp=0;
      break;
    case 16:
      format=AFMT_S16_NE;
      *signp=1;
      break;
    case 24:
      format=AFMT_S24_LE;
      if(*endian)format=AFMT_S24_BE;
      *signp=1;
      break;
    }
    
    /* try to lower the DSP delay; this ioctl may fail gracefully */
    {
      
      long bytesperframe=(local_bits+7)/8*local_ch*input_size;
      int fraglog=ilog(bytesperframe);
      int fragment=0x00040000|fraglog,fragcheck;
      
      fragcheck=fragment;
      int ret=ioctl(fd,SNDCTL_DSP_SETFRAGMENT,&fragment);
      if(ret || fragcheck!=fragment){
	fprintf(stderr,"Could not set DSP fragment size; continuing.\n");
      }
    }
    
    /* format, channels, rate */
    {
      int temp=format;
      int ret=ioctl(fd,SNDCTL_DSP_SETFMT,&temp);
      if(ret || format!=temp) goto failset;
    }
    
    {
      int temp=local_ch;
      int ret=ioctl(fd,SNDCTL_DSP_CHANNELS,&temp);
      if(ret || temp!=local_ch) goto failset;
    }
    
    {
      int temp=rate;
      int ret=ioctl(fd,SNDCTL_DSP_SPEED,&temp);
      if(ret || temp!=rate) goto failset;
    }
    
    failflag=0;
    break;
    
  failset:
    downgrade=1;
    
    /* first try decreasing bit depth */
    if(local_bits==24){
      local_bits=16;
    }else{
      
      /* next, drop channels */
      local_bits=*bits;
      local_ch--;
    }
      
    ioctl(fd,SNDCTL_DSP_RESET);
  }

  if(failflag || downgrade)
    fprintf(stderr,"Unable to set playback for %d bits, %d channels, %dHz\n",
	    *bits,*ch,rate);
  if(downgrade && !failflag)
    fprintf(stderr,"\tUsing %d bits, %d channels, %dHz instead\n",
	    local_bits,local_ch,rate);
  if(failflag)return -1;
  
  *bits=local_bits;
  *ch=local_ch;
  
  return 0;
}

static int output_startup(FILE *f, int devtype, int format, int rate, int *ch, int *endian, int *bits,int *signp){
  switch(devtype){
  case 0: // pipe, regular file or file device 
    switch(format){
    case 0:
      /* WAVE format */
      ftruncate(fileno(f),0);
      WriteWav(f,*ch,rate,*bits,-1);
      *endian=0;
      *signp=1;
      if(*bits==8)*signp=0;
      break;
    case 1:
      /* AIFF-C format */
      ftruncate(fileno(f),0);
      WriteAifc(f,*ch,rate,*bits,-1);
      *endian=1;
      *signp=1;
      break;
    case 2:
      /* raw little endian */
      *endian=0;
      *signp=1;
      if(*bits==8)*signp=0;
      break;
    case 3:
      /* raw big endian */
      *endian=1;
      *signp=1;
      if(*bits==8)*signp=0;
      break;
    default:
      fprintf(stderr,"Unsupported output file format selected!\n");
      return -1;
    }
    return 0;

  case 1: // OSS playback
    return OSS_playback_startup(f, rate, ch, endian, bits, signp);

  default: // undefined for now
    fprintf(stderr,"Unsupported playback device selected\n");
    return -1;
  }
}

static void output_halt(FILE *f,int devtype,int format, int rate, int ch, int endian, int bits,int signp,
			off_t bytecount){
  switch(devtype){
  case 0:
    switch(format){
    case 0:
      WriteWav(f,ch,rate,bits,bytecount); // will complete header only if stream is seekable
      break;
    case 1:
      WriteAifc(f,ch,rate,bits,bytecount); // will complete header only if stream is seekable
      break;
    }
    break;
  case 1: // OSS
    {
      int fd=fileno(f);
      ioctl(fd,SNDCTL_DSP_RESET);
    }
  }
}

static int outpack(time_linkage *link,unsigned char *audiobuf,
		   int ch,int bits,int endian,
		   int signp,int *source){
  int bytes=(bits+7)>>3;
  int32_t signxor=(signp?0:0x800000L);
  int bytestep=bytes*(ch-1);

  int endbytecase=endian*3+(bytes-1);
  int j,i;
  for(j=0;j<ch;j++){
    unsigned char *o=audiobuf+bytes*j;
    if(!mute_channel_muted(link->active,j) && source[j]){
      
      float *d=link->data[j];

      for(i=0;i<link->samples;i++){
	float dval=d[i];
	int32_t val=rint(dval*8388608.);
	if(val>8388607)val=8388607;
	if(val<-8388608)val=-8388608;
	val ^= signxor;
  
	switch(endbytecase){
	case 2:
	  /* LE 24 */
	  *o++=val;
	  // fall through
	case 1:
	  /* LE 16 */
	  *o++=val>>8;
	  // fall through
	case 0:case 3:
	  /* 8 */
	  *o++=val>>16;
	  break;
	case 4:
	  /* BE 16 */
	  *o++=val>>16;
	  *o++=val>>8;
	  break;
	case 5:
	  /* BE 24 */
	  *o++=val>>16;
	  *o++=val>>8;
	  *o++=val;
	  break;
	}

	o+=bytestep;
      }
    }else{
      for(i=0;i<link->samples;i++){
	int32_t val = signxor;
  
	switch(endbytecase){
	case 2:case 5:
	  /* 24 */
	  *o++=val;
	  // fall through
	case 1:case 4:
	  /* LE 16 */
	  *o++=val>>8;
	  // fall through
	case 0:case 3:
	  /* 8 */
	  *o++=val>>16;
	  break;
	}
	
	o+=bytestep;
      }
    }
  }
  return bytes*ch*link->samples;
}

extern pthread_mutex_t input_mutex;
extern mix_settings *mix_set;

/* playback must be halted for new output settings to take hold */
void *playback_thread(void *dummy){
  int i,j,k;
  unsigned char *audiobuf;
  int bigendianp=(AFMT_S16_NE==AFMT_S16_BE?1:0);
  time_linkage *link;
  int result;
  off_t output_bytecount=0;

  int att_last=master_att;

  /* for output feedback */
  float *rms=alloca(sizeof(*rms)*OUTPUT_CHANNELS);
  float *peak=alloca(sizeof(*peak)*OUTPUT_CHANNELS);

  long offset=0;

  /* monitor setup */
  FILE *monitor_fd=NULL;
  int monitor_bits;
  int monitor_ch;
  int monitor_endian=bigendianp;
  int monitor_signp=1;
  int monitor_started=0;
  int monitor_devicenum=outset.monitor.device;

  int stdout_bits;
  int stdout_ch;
  int stdout_endian;
  int stdout_signp=1;
  int stdout_started=0;
  int stdout_format=outset.stdout.format;

  /* inspect mixdown; how many channels are in use? */
  {
    for(j=OUTPUT_CHANNELS-1;j>=0;j--){
      for(i=0;i<input_ch;i++){
	mix_settings *m=mix_set+i;
      
	if(m->placer_destA[j] ||
	   m->placer_destB[j])break;

	for(k=0;k<MIX_BLOCKS;k++)
	  if(m->insert_dest[k][j])break;
	if(k<MIX_BLOCKS)break;
      }
      if(i<input_ch)break;
    }
    monitor_ch=stdout_ch=j+1;
  }

  if(output_monitor_available){
    int ch=outset.monitor.ch;
    int bits=outset.monitor.bytes;

    /* channels */
    switch(ch){
    case 0:
      break;
    case 1:case 2:
      monitor_ch=ch;
      break;
    default:
      monitor_ch=(ch-1)*2;
      break;
    }

    /* bits */
    switch(bits){
    case 0:case 2:
      /* 'auto', 16 */
      monitor_bits=16;
      break;
    case 1:
      monitor_bits=8;
      break;
    case 3:
      monitor_bits=24;
      break;
    }
  }
  
  if(output_stdout_available){
    int ch=outset.stdout.ch;
    int bits=outset.stdout.bytes;

    /* channels */
    switch(ch){
    case 0:
      break;
    case 1:case 2:
      stdout_ch=ch;
      break;
    default:
      stdout_ch=(ch-1)*2;
      break;
    }

    /* bits */
    switch(bits){
    case 0:
      if(output_stdout_device)
	stdout_bits=16;
      else
	stdout_bits=24;
      break;
    case 1:
      stdout_bits=8;
      break;
    case 2:
      stdout_bits=16;
      break;
    case 3:
      stdout_bits=24;
      break;
    }

  }

  audiobuf=malloc(input_size*OUTPUT_CHANNELS*4); // largest possible need
  
  while(1){

    /* the larger lock against seeking is primarily cosmetic, but
       keeps the metadata strictly in sync.  This lock is only against
       seeks. */
    pthread_mutex_lock(&input_mutex);

    if(playback_seeking){
      pipeline_reset();
      playback_seeking=0;
    }
    
    if(playback_exit){
      pthread_mutex_unlock(&input_mutex);
      break;
    }
    
    offset+=input_size;

    /* get data */
    link=input_read();
    result=link->samples;
    pthread_mutex_unlock(&input_mutex);

    /* channel pipeline */
    link=mute_read(link);
    result|=link->samples;
    link=declip_read(link);
    result|=link->samples;
    link=deverb_read_channel(link);
    result|=link->samples;
    link=multicompand_read_channel(link);
    result|=link->samples;
    link=singlecomp_read_channel(link);
    result|=link->samples;
    link=eq_read_channel(link);
    result|=link->samples;

    /* per-channel plate reverb generates more channels than it takes;
       these are swallowed and mixed immediately by mixdown */
    {
      time_linkage *reverbA;
      time_linkage *reverbB;
      link=plate_read_channel(link,&reverbA,&reverbB);

      link=mix_read(link,reverbA,reverbB);
      result|=link->samples;
    }

    /* master pipeline */
    link=multicompand_read_master(link);
    result|=link->samples;
    link=singlecomp_read_master(link);
    result|=link->samples;
    link=eq_read_master(link);
    result|=link->samples;
    link=plate_read_master(link);
    result|=link->samples;

    if(!result)break;
    /************/
    
    /* master att */
    if(link->samples>0){
      int att=master_att;

      if(att==att_last){
	float scale=fromdB(att/10.);
	
	for(i=0;i<link->samples;i++)
	  for(j=0;j<link->channels;j++)
	    link->data[j][i]*=scale;
      }else{
	/* slew-limit the scaling */
	float scale=fromdB(att_last*.1);
	float mult=fromdB((att-att_last)*.1 / input_size);
	
	for(i=0;i<link->samples;i++){
	  for(j=0;j<link->channels;j++)
	    link->data[j][i]*=scale;
	  scale*=mult;
	}
	att_last=att;
      }
    }

    link=limit_read(link);

    /************/

    if(link->samples>0){

      /* final limiting and conversion */
      
      /* monitor output */
      if(output_monitor_available){
	if(outset.panel_active[0]){
	  
	  /* lazy playback init */
	  if(!monitor_started){

	    /* nonblocking open... just in case this is an exclusive
               use device currently used by something else */
	    int mfd=open(monitor_list[monitor_devicenum].file,O_RDWR|O_NONBLOCK);
	    if(mfd==-1){
	      fprintf(stderr,"unable to open audio monitor device %s for playback.\n",
		      monitor_list[monitor_devicenum].file);
	      outpanel_monitor_off();
	    }else{
	      fcntl(mfd,F_SETFL,0); /* unset non-blocking */
	      monitor_fd=fdopen(dup(mfd),"wb");
	      close(mfd);

	      if(monitor_fd==NULL){
		fprintf(stderr,"unable to fdopen audio monitor device %s for playback.\n",
			monitor_list[monitor_devicenum].file);
		outpanel_monitor_off();
	      }else{
		if(setvbuf(monitor_fd, NULL, _IONBF , 0)){
		  fprintf(stderr,"Unable to remove block buffering on audio monitor; continuing\n");
		}

		if(output_startup(monitor_fd,monitor_list[monitor_devicenum].type,0,input_rate,
				  &monitor_ch,&monitor_endian,&monitor_bits,&monitor_signp)){
		  outpanel_monitor_off();
		  fclose(monitor_fd);
		  monitor_fd=NULL;
		}else
		  monitor_started=1;
	      }
	    }
	  }

	  if(monitor_started){
	    int outbytes=outpack(link,audiobuf,monitor_ch,
				 monitor_bits,monitor_endian,monitor_signp,
				 outset.monitor.source);
	    
	    fwrite(audiobuf,1,outbytes,monitor_fd);
	  }
	}else{
	  if(monitor_started){
	    /* halt playback */
	    output_halt(monitor_fd,monitor_list[monitor_devicenum].type,0,input_rate,monitor_ch,
			monitor_endian,monitor_bits,monitor_signp,-1);
	    fclose(monitor_fd);
	    monitor_fd=NULL;
	    monitor_started=0;
	  }
	}
      }
      
      /* standard output */
      if(output_stdout_available){
	if(outset.panel_active[1]){

	  /* lazy playback/header init */
	  if(!stdout_started){
	    if(output_startup(stdout,output_stdout_device,stdout_format,input_rate,
			      &stdout_ch,&stdout_endian,&stdout_bits,&stdout_signp))
	      outpanel_stdout_off();
	    else
	      stdout_started=1;
	  }

	  if(stdout_started){
	    int outbytes=outpack(link,audiobuf,stdout_ch,
				 stdout_bits,stdout_endian,stdout_signp,
				 outset.stdout.source);
	    
	    output_bytecount+=fwrite(audiobuf,1,outbytes,stdout);
	  }
	}else{
	  if(stdout_started){
	    /* if stdout is a device, halt playback.  Otherwise, write nothing */
	    if(output_stdout_device){
	      /* halt playback */
	      output_halt(stdout,output_stdout_device,0,input_rate,stdout_ch,stdout_endian,
			  stdout_bits,stdout_signp,-1);
	      stdout_started=0;
	    }
	  }
	}
      }

      /* feedback */
      memset(rms,0,sizeof(*rms)*OUTPUT_CHANNELS);
      memset(peak,0,sizeof(*peak)*OUTPUT_CHANNELS);
      
      for(j=0;j<OUTPUT_CHANNELS;j++){
	if(!mute_channel_muted(link->active,j))
	  for(i=0;i<link->samples;i++){
	    float dval=link->data[j][i];
	    dval*=dval;
	    if(dval>peak[j])peak[j]=dval;
	    rms[j]+= dval;
	  }
      }

      for(j=0;j<OUTPUT_CHANNELS;j++)
	rms[j]/=link->samples;

      /* inform Lord Vader his shuttle is ready */
      push_output_feedback(peak,rms);
      
      write(eventpipe[1],"",1);
    }
  }

  /* shut down monitor */
  if(monitor_fd){
    if(monitor_started)
      output_halt(monitor_fd,monitor_list[monitor_devicenum].type,0,input_rate,monitor_ch,
		  monitor_endian,monitor_bits,monitor_signp,-1);
    fclose(monitor_fd);
  }

  /* shut down stdout playback or write final header */
  if(stdout_started)
    output_halt(stdout,output_stdout_device,stdout_format,input_rate,stdout_ch,stdout_endian,
		stdout_bits,stdout_signp,output_bytecount);

  pipeline_reset();
  playback_active=0;
  playback_exit=0;
  if(audiobuf)free(audiobuf);

  write(eventpipe[1],"",1);
  return(NULL);
}

/* for access from UI */
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


