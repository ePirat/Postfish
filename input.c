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
int input_seekable;

int input_rate;
int input_ch;
int input_size;

pthread_mutex_t input_mutex=PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

typedef struct {
  FILE *f;
  
  off_t begin;
  off_t end;
  off_t data;

  int bytes;
  int signp;
  int endian;
  char *name;
  
} file_entry;

typedef struct{
  int ch;

  int files;
  file_entry *file_list;

  int current_file_entry_number;
  file_entry *current_file_entry;

} group_entry;

static int groups=0;
static group_entry *group_list=0;

typedef struct input_feedback{
  feedback_generic parent_class;
  off_t   cursor;
  float *rms;
  float *peak;
} input_feedback;

static feedback_generic_pool feedpool;

static time_linkage out;

void input_Acursor_set(off_t c){
  pthread_mutex_lock(&input_mutex);
  Acursor=c;
  pthread_mutex_unlock(&input_mutex);
}

void input_Bcursor_set(off_t c){
  pthread_mutex_lock(&input_mutex);
  Bcursor=c;
  pthread_mutex_unlock(&input_mutex);
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
    input_rate / 100;
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

void input_parse(char *filename,int newgroup){
  if(newgroup){
    /* add a group */
    
    if(!groups){
      group_list=calloc(1,sizeof(*group_list));
    }else{
      group_list=realloc(group_list,sizeof(*group_list)*(groups+1));
      memset(group_list+groups,0,sizeof(*group_list));
    }
    groups++;
  }
      
  {
    group_entry *g=group_list+groups-1;
    file_entry *fe;
    
    if(g->files==0){
      g->file_list=calloc(1,sizeof(*g->file_list));
    }else{
      g->file_list=realloc(g->file_list,
			   sizeof(*g->file_list)*(g->files+1));
      memset(g->file_list+g->files,0,sizeof(*g->file_list));
    }
    fe=g->file_list+g->files;
    g->files++;
    
    fe->name=strdup(filename);
  }
}

/* Macros to read header data */
#define READ_U32_LE(buf) \
        (((buf)[3]<<24)|((buf)[2]<<16)|((buf)[1]<<8)|((buf)[0]&0xff))

#define READ_U16_LE(buf) \
        (((buf)[1]<<8)|((buf)[0]&0xff))

#define READ_U32_BE(buf) \
        (((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|((buf)[3]&0xff))

#define READ_U16_BE(buf) \
        (((buf)[0]<<8)|((buf)[1]&0xff))

double read_IEEE80(unsigned char *buf){
  int s=buf[0]&0xff;
  int e=((buf[0]&0x7f)<<8)|(buf[1]&0xff);
  double f=((unsigned long)(buf[2]&0xff)<<24)|
    ((buf[3]&0xff)<<16)|
    ((buf[4]&0xff)<<8) |
    (buf[5]&0xff);
  
  if(e==32767){
    if(buf[2]&0x80)
      return HUGE_VAL; /* Really NaN, but this won't happen in reality */
    else{
      if(s)
	return -HUGE_VAL;
      else
	return HUGE_VAL;
    }
  }
  
  f=ldexp(f,32);
  f+= ((buf[6]&0xff)<<24)|
    ((buf[7]&0xff)<<16)|
    ((buf[8]&0xff)<<8) |
    (buf[9]&0xff);
  
  return ldexp(f, e-16446);
}

static int find_chunk(FILE *in, char *type, unsigned int *len, int endian){
  unsigned int i;
  unsigned char buf[8];

  while(1){
    if(fread(buf,1,8,in) <8)return 0;

    if(endian)
      *len = READ_U32_BE(buf+4);
    else
      *len = READ_U32_LE(buf+4);

    if(memcmp(buf,type,4)){

      if((*len) & 0x1)(*len)++;
      
      for(i=0;i<*len;i++)
	if(fgetc(in)==EOF)return 0;

    }else return 1;
  }
}

int input_load(void){

  int stdinp=0,i,k;

  input_ch=0;

  if(groups==0){
    /* look at stdin... is it a file, pipe, tty...? */
    if(isatty(STDIN_FILENO)){
      fprintf(stderr,
	      "Postfish requires input either as a list of contiguous WAV\n"
	      "files on the command line, or WAV data piped|redirected to\n"
	      "stdin. postfish -h will give more details.\n");
      return 1;
    }
    stdinp=1;    /* file coming in via stdin */
    
    group_list=calloc(1,sizeof(*group_list));
    group_list[0].file_list=calloc(1,sizeof(*group_list[0].file_list));

    groups=1;
    group_list[0].files=1;
    group_list[0].file_list[0].name="stdin";
  }

  for(k=0;k<groups;k++){
    group_entry *g=group_list+k;
    off_t total=0;

    for(i=0;i<g->files;i++){
      file_entry *fe=g->file_list+i;
      FILE *f;
      char *fname="stdin";

      if(stdinp){
	int newfd=dup(STDIN_FILENO);
	f=fdopen(newfd,"rb");
      }else{
	fname=g->file_list[i].name;
	f=fopen(fname,"rb");
      }

      /* Crappy! Use a lib to do this for pete's sake! */
      if(f){
	unsigned char headerid[12];
	off_t filelength;
	fe->f=f;
	
	/* parse header (well, sort of) and get file size */
	input_seekable=(fseek(f,0,SEEK_CUR)?0:1);
	if(!input_seekable){
	  filelength=-1;
	}else{
	  fseek(f,0,SEEK_END);
	  filelength=ftello(f);
	  fseek(f,0,SEEK_SET);
	}

	fread(headerid,1,12,f);
	if(!strncmp(headerid,"RIFF",4) && !strncmp(headerid+8,"WAVE",4)){
	  unsigned int chunklen;

	  if(find_chunk(f,"fmt ",&chunklen,0)){
	    int ltype;
	    int lch;
	    int lrate;
	    int lbits;
	    unsigned char *buf=alloca(chunklen);
	    
	    fread(buf,1,chunklen,f);
	    
	    ltype = READ_U16_LE(buf); 
	    lch =   READ_U16_LE(buf+2); 
	    lrate = READ_U32_LE(buf+4);
	    lbits = READ_U16_LE(buf+14);
	    
	    if(ltype!=1){
	      fprintf(stderr,"%s:\n\tWAVE file not PCM.\n",fname);
	      return 1;
	    }
	      
	    fe->bytes=(lbits+7)/8;
	    fe->signp=0;
	    fe->endian=0;
	    if(fe->bytes>1)fe->signp=1;
	    
	    if(lrate<4000 || lrate>192000){
	      fprintf(stderr,"%s:\n\tSampling rate out of bounds\n",fname);
	      return 1;
	    }
	    
	    if(k==0 && i==0){
	      input_rate=lrate;
	    }else if(input_rate!=lrate){
	      fprintf(stderr,"%s:\n\tInput files must all be same sampling rate.\n",fname);
	      return 1;
	    }
	    
	    if(i==0){
	      g->ch=lch;
	      input_ch+=lch;
	    }else{
	      if(g->ch!=lch){
		fprintf(stderr,"%s:\n\tInput files must all have same number of channels.\n",fname);
		return 1;
	      }
	    }

	    if(find_chunk(f,"data",&chunklen,0)){
	      off_t pos=ftello(f);
	      
	      if(input_seekable)
		filelength=
		  (filelength-pos)/
		  (g->ch*fe->bytes)*
		  (g->ch*fe->bytes)+pos;
	      
	      if(chunklen==0UL ||
		 chunklen==0x7fffffffUL || 
		 chunklen==0xffffffffUL){
		if(filelength==-1){
		  fe->begin=total;
		  total=fe->end=-1;
		  fprintf(stderr,"%s: Incomplete header; assuming stream.\n",fname);
		}else{
		  fe->begin=total;
		  total=fe->end=total+(filelength-pos)/(g->ch*fe->bytes);
		  fprintf(stderr,"%s: Incomplete header; using actual file size.\n",fname);
		}
	      }else if(filelength==-1 || chunklen+pos<=filelength){
		fe->begin=total;
		total=fe->end=total+ (chunklen/(g->ch*fe->bytes));
		fprintf(stderr,"%s: Using declared file size.\n",fname);

	      }else{
		fe->begin=total;
		total=fe->end=total+(filelength-pos)/(g->ch*fe->bytes);
		fprintf(stderr,"%s: File truncated; Using actual file size.\n",fname);
	      }
	      fe->data=ftello(f);
	    } else {
	      fprintf(stderr,"%s: WAVE file has no \"data\" chunk following \"fmt \".\n",fname);
	      return 1;
	    }
	  }else{
	    fprintf(stderr,"%s: WAVE file has no \"fmt \" chunk.\n",fname);
	    return 1;
	  }

	}else if(!strncmp(headerid,"FORM",4) && !strncmp(headerid+8,"AIF",3)){
	  unsigned int len;
	  int aifc=0;
	  if(headerid[11]=='C')aifc=1;
	  unsigned char *buffer;
	  char buf2[8];

	  int lch;
	  int lbits;
	  int lrate;
	  
	  /* look for COMM */
	  if(!find_chunk(f, "COMM", &len,1)){
	    fprintf(stderr,"%s: AIFF file has no \"COMM\" chunk.\n",fname);
	    return 1;
	  }
	  
	  if(len < 18 || (aifc && len<22)) {
	    fprintf(stderr,"%s: AIFF COMM chunk is truncated.\n",fname);
	    return 1;
	  }
	  
	  buffer = alloca(len);

	  if(fread(buffer,1,len,f) < len){
	    fprintf(stderr, "%s: Unexpected EOF in reading AIFF header\n",fname);
	    return 1;
	  }

	  lch = READ_U16_BE(buffer);
	  lbits = READ_U16_BE(buffer+6);
	  lrate = (int)read_IEEE80(buffer+8);

	  fe->endian = 1; // default
	      
	  fe->bytes=(lbits+7)/8;
	  fe->signp=1;
	    
	  if(lrate<4000 || lrate>192000){
	    fprintf(stderr,"%s:\n\tSampling rate out of bounds\n",fname);
	    return 1;
	  }

	  if(k==0 && i==0){
	    input_rate=lrate;
	  }else if(input_rate!=lrate){
	    fprintf(stderr,"%s:\n\tInput files must all be same sampling rate.\n",fname);
	    return 1;
	  }
	    
	  if(i==0){
	    g->ch=lch;
	    input_ch+=lch;
	  }else{
	    if(g->ch!=lch){
	      fprintf(stderr,"%s:\n\tInput files must all have same number of channels.\n",fname);
	      return 1;
	    }
	  }

	  if(aifc){
	    if(!memcmp(buffer+18, "NONE", 4)) {
	      fe->endian = 1;
	    }else if(!memcmp(buffer+18, "sowt", 4)) {
	      fe->endian = 0;
	    }else{
	      fprintf(stderr, "%s: Postfish supports only linear PCM AIFF-C files.\n",fname);
	      return 1;
	    }
	  }

	  if(!find_chunk(f, "SSND", &len, 1)){
	    fprintf(stderr,"%s: AIFF file has no \"SSND\" chunk.\n",fname);
	    return 1;
	  }

	  if(fread(buf2,1,8,f) < 8){
	    fprintf(stderr,"%s: Unexpected EOF reading AIFF header\n",fname);
	    return 1;
	  }
	  
	  {
	    int loffset = READ_U32_BE(buf2);
	    int lblocksize = READ_U32_BE(buf2+4);

	    /* swallow some data */
	    for(i=0;i<loffset;i++)
	      if(fgetc(f)==EOF)break;
	    
	    if( lblocksize == 0 && (lbits == 32 || lbits == 24 || lbits == 16 || lbits == 8)){

	      off_t pos=ftello(f);
	      
	      if(input_seekable)
		filelength=
		  (filelength-pos)/
		  (g->ch*fe->bytes)*
		  (g->ch*fe->bytes)+pos;
	      
	      if(len==0UL ||
		 len==0x7fffffffUL || 
		 len==0xffffffffUL){
		if(filelength==-1){
		  fe->begin=total;
		  total=fe->end=-1;
		  fprintf(stderr,"%s: Incomplete header; assuming stream.\n",fname);
		}else{
		  fe->begin=total;
		  total=fe->end=total+(filelength-pos)/(g->ch*fe->bytes);
		  fprintf(stderr,"%s: Incomplete header; using actual file size.\n",fname);
		}
	      }else if(filelength==-1 || (len+pos-loffset-8)<=filelength){
		fe->begin=total;
		total=fe->end=total+ ((len-loffset-8)/(g->ch*fe->bytes));
		fprintf(stderr,"%s: Using declared file size.\n",fname);

	      }else{
		fe->begin=total;
		total=fe->end=total+(filelength-pos)/(g->ch*fe->bytes);
		fprintf(stderr,"%s: File truncated; Using actual file size.\n",fname);
	      }
	      fe->data=pos;
	    }else{
	      fprintf(stderr, "%s: Postfish supports only linear PCM AIFF-C files.\n",fname);
	      return 1;
	    }
	  }

	} else {

	  fprintf(stderr,"%s: Postfish supports only linear PCM WAV and AIFF[-C] files.\n",fname);
	  return 1;
	}

      }else{
	fprintf(stderr,"%s: Unable to open file.\n",fname);
	return 1;
      }
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

  if(input_rate<6000){
    input_size=256;
  }else if(input_rate<15000){
    input_size=512;
  }else if(input_rate<25000){
    input_size=1024;
  }else if(input_rate<50000){
    input_size=2048;
  }else if(input_rate<100000){
    input_size=4096;
  }else
    input_size=8192;

  out.channels=input_ch;

  out.data=malloc(sizeof(*out.data)*input_ch);
  for(i=0;i<input_ch;i++)
    out.data[i]=malloc(sizeof(**out.data)*input_size);
  
  return 0;
}

static off_t input_seek_i(off_t pos,int ps){
  int i,k;
  int flag=0;
  off_t maxpos=0;

  if(pos<0)pos=0;
  if(!input_seekable){
    for(i=0;i<groups;i++){
      group_list[i].current_file_entry=group_list[i].file_list;
      group_list[i].current_file_entry_number=0;
    }
    return -1;
  }

  pthread_mutex_lock(&input_mutex);
  if(ps)playback_seeking=1;

  /* seek has to happen correctly in all groups */
  for(k=0;k<groups;k++){
    group_entry *g=group_list+k;

    for(i=0;i<g->files;i++){
      file_entry *fe=g->current_file_entry=g->file_list+i;
      g->current_file_entry_number=i;

      if(fe->begin<=pos && fe->end>pos){
	flag=1;
	fseeko(fe->f, 
	       (pos-fe->begin)*(g->ch*fe->bytes)+fe->data,
	       SEEK_SET);
	break;
      }
    }
    
    if(i==g->files){
      /* this group isn't that long; seek to the end of it */
      file_entry *fe=g->current_file_entry;
      
      fseeko(fe->f,(fe->end-fe->begin)*(g->ch*fe->bytes)+fe->data,SEEK_SET);
      
      if(fe->end>maxpos)maxpos=fe->end;
    }
  }

  if(flag){
    cursor=pos;
    pthread_mutex_unlock(&input_mutex);
  }else{
    cursor=maxpos;
    pthread_mutex_unlock(&input_mutex);
  }

  return cursor;
}

off_t input_seek(off_t pos){
  return input_seek_i(pos,1);
}

off_t input_time_seek_rel(float s){
  return input_seek(cursor+input_rate*s);
}

static feedback_generic *new_input_feedback(void){
  input_feedback *ret=malloc(sizeof(*ret));
  ret->rms=malloc(input_ch*sizeof(*ret->rms));
  ret->peak=malloc(input_ch*sizeof(*ret->peak));
  return (feedback_generic *)ret;
}

static void push_input_feedback(float *peak,float *rms, off_t cursor){
  input_feedback *f=(input_feedback *)
    feedback_new(&feedpool,new_input_feedback);
  f->cursor=cursor;
  memcpy(f->rms,rms,input_ch*sizeof(*rms));
  memcpy(f->peak,peak,input_ch*sizeof(*peak));
  feedback_push(&feedpool,(feedback_generic *)f);
}

int pull_input_feedback(float *peak,float *rms,off_t *cursor){
  input_feedback *f=(input_feedback *)feedback_pull(&feedpool);
  if(!f)return 0;
  if(rms)memcpy(rms,f->rms,sizeof(*rms)*input_ch);
  if(peak)memcpy(peak,f->peak,sizeof(*peak)*input_ch);
  if(cursor)*cursor=f->cursor;
  feedback_old(&feedpool,(feedback_generic *)f);
  return 1;
}

static void LEconvert(float **data,
		      unsigned char *readbuf, int dataoff,
		      int ch,int bytes, int signp, int n){
  int i,j,k=0;
  int32_t xor=(signp?0:0x80000000UL);
  float scale=1./2147483648.;

  k=0;
  switch(bytes){
  case 1:
    
    for(i=dataoff;i<dataoff+n;i++)
      for(j=0;j<ch;j++){
	data[j][i]=((readbuf[k]<<24)^xor)*scale;
	k++;
      }
    break;

  case 2:

    for(i=dataoff;i<dataoff+n;i++)
      for(j=0;j<ch;j++){
	data[j][i]=(((readbuf[k]<<16)|(readbuf[k+1]<<24))^xor)*scale;
	k+=2;
      }
    break;

  case 3:

    for(i=dataoff;i<dataoff+n;i++)
      for(j=0;j<ch;j++){
	data[j][i]=(((readbuf[k]<<8)|(readbuf[k+1]<<16)|(readbuf[k+2]<<24))^xor)*scale;
	k+=3;
      }
    break;

  case 4:

    for(i=dataoff;i<dataoff+n;i++)
      for(j=0;j<ch;j++){
	data[j][i]=(((readbuf[k])|(readbuf[k+1]<<8)|(readbuf[k+2]<<16)|(readbuf[k+3]<<24))^xor)*scale;
	k+=4;
      }
    break;
  }
}

static void BEconvert(float **data,
		      unsigned char *readbuf, int dataoff,
		      int ch,int bytes, int signp, int n){
  int i,j,k=0;
  int32_t xor=(signp?0:0x80000000UL);
  float scale=1./2147483648.;

  k=0;
  switch(bytes){
  case 1:
    
    for(i=dataoff;i<dataoff+n;i++)
      for(j=0;j<ch;j++){
	data[j][i]=((readbuf[k]<<24)^xor)*scale;
	k++;
      }
    break;

  case 2:

    for(i=dataoff;i<dataoff+n;i++)
      for(j=0;j<ch;j++){
	data[j][i]=(((readbuf[k+1]<<16)|(readbuf[k]<<24))^xor)*scale;
	k+=2;
      }
    break;

  case 3:

    for(i=dataoff;i<dataoff+n;i++)
      for(j=0;j<ch;j++){
	data[j][i]=(((readbuf[k+2]<<8)|(readbuf[k+1]<<16)|(readbuf[k]<<24))^xor)*scale;
	k+=3;
      }
    break;

  case 4:

    for(i=dataoff;i<dataoff+n;i++)
      for(j=0;j<ch;j++){
	data[j][i]=(((readbuf[k+3])|(readbuf[k+2]<<8)|(readbuf[k+1]<<16)|(readbuf[k]<<24))^xor)*scale;
	k+=4;
      }
    break;
  }
}

static void zero(float **data, int dataoff, int ch, int n){
  int i,j;
  
  for(i=dataoff;i<dataoff+n;i++)
    for(j=0;j<ch;j++)
      data[j][i]=0;
}

/* no locking within as the only use of input_read is locked in the
   playback thread (must be locked there because the real lock needs
   to avoid a seeking race) */

time_linkage *input_read(void){
  int h,i,j;
  int groupread_s=0;

  float *rms=alloca(sizeof(*rms)*(out.channels));
  float *peak=alloca(sizeof(*peak)*(out.channels));

  memset(rms,0,sizeof(*rms)*(out.channels));
  memset(peak,0,sizeof(*peak)*(out.channels));

  out.samples=0;

  /* the non-streaming case */
  if(!loop_active && input_seekable){
    for(i=0;i<groups;i++)
      if(cursor<group_list[i].file_list[group_list[i].files-1].end)
	break;
    if(i==groups)goto tidy_up;
  }

  /* the streaming case */
  if(!input_seekable && feof(group_list[0].current_file_entry->f)){ 
    goto tidy_up;
  }

  /* If we're A-B looping, we might need several loops/seeks */
  while(groupread_s<input_size){
    int chcount=0;
    int max_read_s=0;

    if(loop_active && cursor>=Bcursor){
      input_seek_i(Acursor,0);
    }

    /* each read section is by group */
    for(h=0;h<groups;h++){
      group_entry *g=group_list+h;
      int toread_s=input_size-groupread_s;
      int fileread_s=0;
      
      if(input_seekable && loop_active && toread_s>Bcursor-cursor)
	toread_s = Bcursor-cursor;
      
      /* inner loop in case the read spans multiple files within the group */
      while(toread_s){
	file_entry *fe=g->current_file_entry;
	off_t ret;

	/* span forward to next file entry in the group? */
	if(cursor+fileread_s>=fe->end && 
	   g->current_file_entry_number+1<g->files){
	  fe=++g->current_file_entry;
	  g->current_file_entry_number++;
	  fseeko(fe->f,fe->data,SEEK_SET);
	}

	/* perform read/conversion of this file entry */
	{
	  off_t read_this_loop=fe->end-cursor-fileread_s;
	  unsigned char readbuf[input_size*(g->ch*fe->bytes)];
	  if(read_this_loop>toread_s)read_this_loop=toread_s;
	  
	  ret=fread(readbuf,1,read_this_loop*(g->ch*fe->bytes),fe->f);
	  
	  if(ret>0){
	    ret/=(g->ch*fe->bytes);
	    
	    if(fe->endian)
	      BEconvert(out.data+chcount,readbuf,
			fileread_s+groupread_s,g->ch,fe->bytes,fe->signp,ret);
	    else
	      LEconvert(out.data+chcount,readbuf,
			fileread_s+groupread_s,g->ch,fe->bytes,fe->signp,ret);
	    
	    fileread_s+=ret;
	    toread_s-=ret;

	  }else{
	    if(g->current_file_entry_number+1>=g->files){

	      /* end of group before full frame */	      
	      zero(out.data+chcount,fileread_s+groupread_s,g->ch,toread_s);
	      toread_s=0;
	    }
	  }
	}
      }

      if(max_read_s<fileread_s)max_read_s=fileread_s;
      chcount+=g->ch;
    }

    groupread_s+=max_read_s;
    cursor+=max_read_s;

    if(!loop_active || cursor<Bcursor) break;

  }

  out.samples=groupread_s;

  for(i=0;i<groupread_s;i++)
    for(j=0;j<out.channels;j++){
      float dval=out.data[j][i];
      dval*=dval;
      if(dval>peak[j])peak[j]=dval;
      rms[j]+= dval;
    }
  
  for(j=0;j<out.channels;j++)
    rms[j]/=out.samples;

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




