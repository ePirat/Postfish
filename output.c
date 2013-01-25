/*
 *
 *  postfish
 *    
 *      Copyright (C) 2002-2012 Monty
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

/* sound playback now uses AO, but device detection only looks for OSS/ALSA/PULSE + the AO default*/
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ao/ao.h>
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
#include "freeverb.h"

output_settings outset;

extern int input_size;
extern int input_rate;
sig_atomic_t playback_active=0;
sig_atomic_t playback_exit=0;
sig_atomic_t playback_seeking=0;
sig_atomic_t master_att;

static inline int host_is_big_endian() {
  union {
    int32_t pattern;
    unsigned char bytewise[4];
  } m;
  m.pattern = 0xfeedface; /* deadbeef */
  if (m.bytewise[0] == 0xfe) return 1;
  return 0;
}

void output_reset(void){
  /* empty feedback queues */
  while(pull_output_feedback(NULL,NULL));
  return;
}

void pipeline_reset(){
  if(eventpipe[0]>-1){
    int flags=fcntl(eventpipe[0],F_GETFL);
    char buf[1];
    /* drain the event pipe */
    if(fcntl(eventpipe[0],F_SETFL,O_NONBLOCK))
      fprintf(stderr,"Unable to set O_NONBLOCK on event pipe.\n");
    while(read(eventpipe[0],buf,1)>0);
    fcntl(eventpipe[0],F_SETFL,flags);
  }

  input_reset();  /* clear any persistent lapping state */
  declip_reset();  /* clear any persistent lapping state */
  eq_reset();      /* clear any persistent lapping state */
  multicompand_reset(); /* clear any persistent lapping state */
  singlecomp_reset(); /* clear any persistent lapping state */
  deverb_reset(); /* clear any persistent lapping state */
  limit_reset(); /* clear any persistent lapping state */
  output_reset(); /* clear any persistent lapping state */
  mix_reset();
  p_reverb_reset();
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

output_monitor_entry *monitor_list;
int monitor_entries;

/* look for sound devices that actually exist */

static void add_monitor(char *file, char *name,int driver){
  monitor_entries++;
  if(monitor_list){
    monitor_list=realloc(monitor_list,
			 monitor_entries*sizeof(*monitor_list));
  }else
    monitor_list=malloc(monitor_entries*sizeof(*monitor_list));

  monitor_list[monitor_entries-1].file=file?strdup(file):NULL;
  monitor_list[monitor_entries-1].name=strdup(name);
  monitor_list[monitor_entries-1].driver=driver;
}

char *search_for_major_minor(char *dir,int major,int minor){
  /* open the directory, stat entries one by one */
  DIR *d=opendir(dir);
  struct dirent *de;

  if(d==NULL) return NULL;

  while((de=readdir(d))){
    struct stat s;
    char buf[PATH_MAX];
    snprintf(buf,PATH_MAX,"%s/%s",dir,de->d_name);
    if(!stat(buf,&s)){
      if (major==((int)(s.st_rdev>>8)&0xff) &&
          minor==((int)(s.st_rdev&0xff))){
        closedir(d);
        return strdup(buf);
      }
    }
  }

  closedir(d);
  return NULL;
}

static int is_oss(int f){
  int id = ao_driver_id("oss");
  struct stat s;

  if(id>=0 && isachr(f))
    if(!fstat(f,&s)){
      int major=(int)(s.st_rdev>>8)&0xff;
      int minor=(int)(s.st_rdev&0xff);

      /* is this a Linux OSS audio device (Major 14)? */
      if(major==14){
        char *output_stdout_devicename = search_for_major_minor("/dev",major,minor);
        if(!output_stdout_devicename)
          output_stdout_devicename = search_for_major_minor("/dev/sound",major,minor);
        if(!output_stdout_devicename){
          fprintf(stderr,"OSS: Unable to find sound device matching stdout.\n");
          exit(1);
        }

        add_monitor(output_stdout_devicename,output_stdout_devicename,id);
        free(output_stdout_devicename);
        fclose(stdout);
        stdout=NULL;
        return 1;
      }
    }

  return 0;
}

static int is_alsa(int f){
  struct stat s;
  int id = ao_driver_id("alsa");

  if(id>=0 && isachr(f))
    if(!fstat(f,&s)){
      int major=(int)(s.st_rdev>>8)&0xff;
      int minor=(int)(s.st_rdev&0xff);
      char buffer[80];

      /* is this a Linux ALSA audio device (Major 116)? */
      if(major==116){
        int ret,c,d;

        char *output_stdout_devicename = search_for_major_minor("/dev/snd",major,minor);
        if(!output_stdout_devicename){
          fprintf(stderr,"ALSA: Unable to find sound device matching stdout.\n");
          exit(1);
        }

        /* parse the ALSA id from the device name */
        ret=sscanf(output_stdout_devicename,"/dev/snd/pcmC%dD%dp",&c,&d);
        if(ret!=2){
          fprintf(stderr,"ALSA: Unable to find sound device matching stdout.\n");
          exit(1);
        }
        free(output_stdout_devicename);
        sprintf(buffer,"hw:%d,%d",c,d);
        add_monitor(buffer,buffer,id);
        fclose(stdout);
        stdout=NULL;
        return 1;
      }
    }

  return 0;
}

static int isaudio(int outfileno){
  if(is_oss(outfileno))return 1;
  if(is_alsa(outfileno))return 2;
  return 0;
}

int output_stdout_available=0;
int output_stdout_ao=0; 
int output_monitor_available=0;

int output_probe_stdout(int outfileno){
  int ret;

  if(isatty(outfileno)){
    /* stdout is the terminal; disable stdout */
    output_stdout_available=0;

  }else if (isareg(outfileno)){
    /* stdout is a regular file */
    output_stdout_available=1;
    output_stdout_ao=0;

  }else if((ret=isaudio(outfileno))){
    /* stdout is an audio device; we don't actually use stdout, we'll reopen as an ao_device */

    output_stdout_available=1;
    output_stdout_ao=1;

  }else{
    /* God only knows what stdout is.  It might be /dev/null or some other.... treat it similar to file */

    output_stdout_available=1;
    output_stdout_ao=0;

  }

  return 0;
}

static void output_probe_monitor_OSS(){
  /* open /dev and search of major 14 */
  int id = ao_driver_id("oss");

  if(id>=0){
    DIR *d=opendir("/dev");
    struct dirent *de;

    while((de=readdir(d))){
      struct stat s;
      char buf[PATH_MAX];
      snprintf(buf,PATH_MAX,"/dev/%s",de->d_name);
      if(!stat(buf,&s)){

        int major=(int)(s.st_rdev>>8)&0xff;
        int minor=(int)(s.st_rdev&0xff);

        /* is this a Linux OSS dsp audio device? (Major 14, minor 3,19...[only list /dev/dspX]) */
        if(major==14 && (minor&0xf)==3){
          int f=open(buf,O_RDWR|O_NONBLOCK);
          if(f!=-1){
            char name[80];
            snprintf(name,80,"OSS %s",de->d_name);
            add_monitor(buf,name,id);
            close(f);
          }
        }
      }
    }

    closedir(d);
  }
}

static void output_probe_monitor_ALSA(){
  /* does this AO even have alsa output? */
  int id = ao_driver_id("alsa");
  if(id>=0){

    /* test adding a default plug device; the format doesn't matter */
    ao_sample_format format={16,48000,2,AO_FMT_NATIVE,NULL};
    ao_option opt1={"dev","default",0};
    ao_option opt={"client_name","postfish",&opt1};
    ao_device *test=ao_open_live(id, &format, &opt);

    if(test){
      add_monitor("default","ALSA default",id);
      ao_close(test);

      /* discover hardware devices from /proc/asound/pcm */
      {
        FILE *pcm=fopen("/proc/asound/pcm","r");
        if(pcm){
          char buf[1024];
          while(fgets(buf,1024,pcm)){
            /* is this a playback device? */
            if(strstr(buf,": playback ")){
              int c,d;
              char desc[80];
              /* parse the card and device numbers, and the name */
              if(sscanf(buf,"%d-%d: %*[^:]: %79[^:]",&c,&d,desc)==3){
                /* add a monitor device */
                snprintf(buf,1024,"ALSA %s",desc);
                snprintf(desc,80,"hw:%d,%d",c,d);
                add_monitor(desc,buf,id);
              }
            }
          }
          fclose(pcm);
        }
      }
    }
  }
}

static void output_probe_monitor_pulse(){
  /* does this AO even have pulse output? */
  int id = ao_driver_id("pulse");
  if(id>=0){
    /* test open; format doesn't matter */
    ao_sample_format format={16,48000,2,AO_FMT_NATIVE,NULL};
    ao_option opt={"client_name","postfish",NULL};
    ao_device *test=ao_open_live(id, &format, &opt);

    if(test){
      add_monitor(NULL,"PulseAudio",id);
      ao_close(test);
    }
  }
}

static int moncomp(const void *a, const void *b){
  output_monitor_entry *ma=(output_monitor_entry *)a;
  output_monitor_entry *mb=(output_monitor_entry *)b;
  return(strcmp(ma->name,mb->name));
}

int output_probe_monitor(void ){
  if(output_stdout_ao!=0){
    output_monitor_available=0;
    return 0;
  }

  output_probe_monitor_pulse();
  {
    int n = monitor_entries;
    output_probe_monitor_ALSA();
    if(monitor_entries>n){
      if(monitor_entries+1>n)
        qsort(monitor_list+n+1, monitor_entries-n-1, sizeof(*monitor_list), moncomp);
    }else{
      /* only search for OSS if we don't find ALSA */
      output_probe_monitor_OSS();
      if(monitor_entries<n){
        qsort(monitor_list+n, monitor_entries-n, sizeof(*monitor_list), moncomp);
      }
    }
  }

  if(monitor_entries>0)
    output_monitor_available=1;

  return 0;
}

static ao_device *playback_startup(int driver,char *device, int rate, int *ch, int *endian, int *bits,int *signp){
  ao_device *ret = NULL;
  int downgrade=0;
  int downgrade_dev=0;
  int local_ch=*ch;
  int local_bits=*bits;
  char localdevice[80];

  *endian=host_is_big_endian();
  if(device)
    strncpy(localdevice,device,80);

  /* try to configure requested playback.  If it fails, keep dropping back
     until one works */
  while(local_ch || local_bits>16){
    ao_sample_format format;
    ao_option opt2={"buffer_time","50",0};
    ao_option opt1={"dev",localdevice,&opt2};
    ao_option opt={"client_name","postfish",&opt1};
    if(!device)opt.next=0;

    format.rate=rate;
    format.channels=local_ch;
    format.byte_format=AO_FMT_NATIVE;
    format.matrix=0;

    switch(local_bits){
    case 8:
      format.bits=8;
      *signp=0;
      break;
    case 16:
      format.bits=16;
      *signp=1;
      break;
    case 24:
      format.bits=24;
      *signp=1;
      break;
    }

    ret = ao_open_live(driver,&format,&opt);
    if(ret) break;

    /* first, if this is ALSA, use a plughw instead of a hw device */
    if(driver==ao_driver_id("alsa") && strstr(localdevice,"hw:")==localdevice){
      snprintf(localdevice,80,"plug%s",device);
      downgrade_dev=1;
    }else{
      /* first try decreasing bit depth */
      downgrade=1;
      if(local_bits==24){
        local_bits=16;
      }else{
        /* next, drop channels */
        local_bits=*bits;
        local_ch--;
      }
    }
  }

  if(!ret || downgrade_dev || downgrade)
    fprintf(stderr,"Unable to open %s for %d bits, %d channels, %dHz\n",
	    device,*bits,*ch,rate);
  if(downgrade && ret)
    fprintf(stderr,"\tUsing %d bits, %d channels, %dHz instead\n",
	    local_bits,local_ch,rate);
  else if(downgrade_dev && ret)
    fprintf(stderr,"\tUsing plug%s instead\n",
	    device);

  *bits=local_bits;
  *ch=local_ch;

  return ret;
}

static int file_startup(FILE *f, int format, int rate, int *ch, int *endian, int *bits,int *signp){
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
}

static void file_halt(FILE *f,int format, int rate, int ch, int endian, int bits,int signp,
                      off_t bytecount){
  switch(format){
  case 0:
    WriteWav(f,ch,rate,bits,bytecount); // will complete header only if stream is seekable
    break;
  case 1:
    WriteAifc(f,ch,rate,bits,bytecount); // will complete header only if stream is seekable
    break;
  }
}

static int outpack(time_linkage *link,unsigned char *audiobuf,
		   int ch,int bits,int endian,
		   int signp,int *source){
  int bytes=(bits+7)>>3;
  int32_t signxor=(signp?0:0x800000L);
  int bytestep=bytes*(ch-1);
  int round=0;

  int endbytecase=endian*3+(bytes-1);
  int j,i;

  switch(endbytecase){
  case 1:case 4:
    round=128;
    break;
  case 0:case 3:
    round=32768;
    break;
  }

  for(j=0;j<ch;j++){
    unsigned char *o=audiobuf+bytes*j;
    if(!mute_channel_muted(link->active,j) && source[j]){

      float *d=link->data[j];

      for(i=0;i<link->samples;i++){
	float dval=d[i];
	int32_t val=rint(dval*8388608.)+round;
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
  int bigendianp=host_is_big_endian();
  time_linkage *link;
  int result;
  off_t output_bytecount=0;

  int att_last=master_att;

  /* for output feedback */
  float *rms=alloca(sizeof(*rms)*OUTPUT_CHANNELS);
  float *peak=alloca(sizeof(*peak)*OUTPUT_CHANNELS);

  long offset=0;

  /* monitor setup */
  ao_device *monitor_device=NULL;
  int monitor_bits;
  int monitor_ch;
  int monitor_endian=bigendianp;
  int monitor_signp=1;
  int monitor_started=0;
  int monitor_devicenum=outset.monitor.device;

  ao_device *stdout_device=NULL;
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
      if(output_stdout_ao)
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
      link=p_reverb_read_channel(link,&reverbA,&reverbB);

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
    link=p_reverb_read_master(link);
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

            monitor_device =
              playback_startup(monitor_list[monitor_devicenum].driver,
                               monitor_list[monitor_devicenum].file,
                               input_rate,
                               &monitor_ch,&monitor_endian,&monitor_bits,&monitor_signp);
            if(!monitor_device){
              outpanel_monitor_off();
	      fprintf(stderr,"unable to open audio monitor device %s (%s) for playback.\n",
		      monitor_list[monitor_devicenum].file,monitor_list[monitor_devicenum].name);
	      outpanel_monitor_off();
	    }else{
              monitor_started=1;
	    }
	  }

	  if(monitor_started){
	    int outbytes=outpack(link,audiobuf,monitor_ch,
				 monitor_bits,monitor_endian,monitor_signp,
				 outset.monitor.source);
            ao_play(monitor_device,(char *)audiobuf,outbytes);
	  }
	}else{
	  if(monitor_started){
	    /* halt playback */
            ao_close(monitor_device);
	    monitor_device=NULL;
	    monitor_started=0;
	  }
	}
      }

      /* standard output */
      if(output_stdout_available){
	if(outset.panel_active[1]){

	  /* lazy playback/header init */
	  if(!stdout_started){
            if(output_stdout_ao){ // AO device
              stdout_device =
                playback_startup(monitor_list[0].driver,
                                 monitor_list[0].file,
                                 input_rate,
                                 &stdout_ch,&stdout_endian,&stdout_bits,&stdout_signp);
              if(!stdout_device){
                outpanel_stdout_off();
                fprintf(stderr,"unable to open stdout device for playback.\n");
              }else
                stdout_started=1;
            }else{
              if(file_startup(stdout,stdout_format,input_rate,
			      &stdout_ch,&stdout_endian,&stdout_bits,&stdout_signp))
                outpanel_stdout_off();
              else
                stdout_started=1;
            }
          }

	  if(stdout_started){
	    int outbytes=outpack(link,audiobuf,stdout_ch,
				 stdout_bits,stdout_endian,stdout_signp,
				 outset.stdout.source);
            if(output_stdout_ao) // AO device
              ao_play(stdout_device,(char *)audiobuf,outbytes);
            else
              output_bytecount+=fwrite(audiobuf,1,outbytes,stdout);
	  }
	}else{
	  if(stdout_started){
	    /* if stdout is a device, halt playback.  Otherwise, write nothing */
	    if(output_stdout_ao){
              ao_close(stdout_device);
              stdout_device=NULL;
	      stdout_started=0;
	    }
	  }
	}
      }

      /* feedback */
      if(eventpipe[1]>-1){
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
  }

  /* shut down monitor */
  if(monitor_device){
    if(monitor_started)
      ao_close(monitor_device);
  }

  /* shut down stdout playback or write final header */
  if(stdout_started){
    if(output_stdout_ao)
      ao_close(stdout_device);
    else
      file_halt(stdout,stdout_format,input_rate,stdout_ch,stdout_endian,
		stdout_bits,stdout_signp,output_bytecount);
  }

  pipeline_reset();
  playback_active=0;
  playback_exit=0;
  if(audiobuf)free(audiobuf);

  if(eventpipe[i]>-1) write(eventpipe[1],"",1);
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


