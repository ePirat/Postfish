/*
 *
 *  postfish
 *    
 *      Copyright (C) 2002-2005 Monty
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
#include "postfish.h"
#include <signal.h>
#include <getopt.h>
#include <fenv.h>  // Thank you C99!
#include <fftw3.h>
#include <gtk/gtk.h>
#include "input.h"
#include "output.h"
#include "declip.h"
#include "eq.h"
#include "deverb.h"
#include "multicompand.h"
#include "singlecomp.h"
#include "limit.h"
#include "mute.h"
#include "mix.h"
#include "freeverb.h"
#include "version.h"
#include "config.h"
#include "mainpanel.h"

int eventpipe[2];
sig_atomic_t main_looping;
char *configfile="postfish-staterc";
char *version;

void clean_exit(int sig){
  signal(sig,SIG_IGN);
  if(sig!=SIGINT){
    fprintf(stderr,
	    "\nTrapped signal %d; saving state and exiting!\n"
	    "This signal almost certainly indicates a bug in the Postfish;\n"
	    "If this version of Postfish is newer than a few months old,\n"
	    "please email a detailed report of the crash along with\n"
	    "processor type, OS version, FFTW3 version, and as much\n"
	    "information as possible about what caused the crash.  The best\n"
	    "possible report will outline the exact steps needed to\n"
	    "reproduce the error, ensuring that we at Xiph can fix the\n"
	    "bug as quickly as possible.\n\n"
	    "-- monty@xiph.org, Postfish revision %s\n\n",sig,version);
    configfile="postfish-staterc-crashsave";
  }else{
    output_halt_playback();
  }

  save_state();

  if(main_looping){
    main_looping=0;
    gtk_main_quit();
  }
  exit(0);
}

const char *optstring = "-c:gh";

struct option options [] = {
        {"configuration-file",required_argument,NULL,'c'},
        {"group",no_argument,NULL,'g'},
        {"help",no_argument,NULL,'h'},

        {NULL,0,NULL,0}
};

static void usage(FILE *f){
  fprintf( f,
"\nthe Postfish, revision %s\n\n"

"USAGE:\n"
"  postfish [options] infile [infile]+ [-g infile [infile]+]+ > output\n\n"

"OPTIONS:\n"
"  -c --configuration-file    : load state from alternate configuration file\n"
"  -g --group                 : place following input files in a new channel\n"
"                               grouping\n"
"  -h --help                  : print this help\n\n"
"INPUT:\n\n"

" Postfish takes WAV/AIFF input either from stdin or from a list of files\n"
" specified on the command line.  A list of input files is handled as\n"
" time-continguous entries, each holding audio data that continues at\n"
" the instant the previous file ends.  Files may also be arranged into\n"
" groups with -g; each group represents additional input channels\n"
" parallel to preceeding groups. All input files must be the same\n"
" sampling rate.  Files in a group must have the same number of\n"
" channels.\n\n"

" Examples:\n\n"
" Files a.wav, b.wav, c.wav and d.wav are all four channels and\n"
" ten minutes each.\n\n"

" postfish a.wav b.wav c.wav d.wav \n"
"   This command line treats the input as forty minutes of four channel\n"
"   audio in the order a.wav, b.wav, c.wav, d.wav.\n\n"

" postfish a.wav b.wav -g c.wav d.wav \n"
"   This command line treats the input as twenty minutes of eight channel\n"
"   audio.  Channels 1-4 are taken from files a.wav and b.wav while channels\n"
"   5-8 are taken from files c.wav  and d.wav.\n\n"

" cat a.wav | postfish \n"
"   This command line sends a.wav to Postfish as a non-seekable stream\n"
"   of four-channel data. If the WAV (or AIFF) header is complete, Postfish\n"
"   obeys the length encoded in the header and halts after processing to\n"
"   that length.  If the data length in the header is unset (0 or -1),\n"
"   Postfish will continue processing data until EOF on stdin.\n\n"

"OUTPUT:\n\n"

" Postfish writes output to stdout.\n\n" 

" If stdout is piped, the output is nonseekable and Postfish marks the\n"
" produced header incomplete (length of -1).  Stopping and re-starting\n"
" processing writes a fresh stream to stdout.\n\n"

" If stdout is redirected to a file, Postfish will write a complete header\n"
" upon processing halt or program exit.  If processing halts and restarts,\n"
" the file is re-written from scratch.\n\n"

" If stdout is a pipe or redirected to a file, the user may specify\n"
" parallel audio monitor through the audio device using the 'mOn' activator\n"
" button in the main panel's 'master' section, or on the output config\n"
" panel.  The audio device selected for playback is configurable on the\n"
" output config panel.\n\n"

" If stdout is redirected to an audio device, output is sent to that audio\n"
" device exclusively and the 'mOn' activator on the main panel will not\n"
" be available.\n\n"

"STATE/CONFIG:\n\n"

" By default, persistent panel state is loaded from the file \n"
" 'postfish-staterc' in the current working directory.  Postfish rewrites\n"
" this file with all current panel state upon exit.  -c specifies loading\n"
" from and saving to an alternate configuration file name.\n\n",version);

}

void parse_command_line(int argc, char **argv){
  int c,long_option_index;
  int newgroup=1;

  while((c=getopt_long(argc,argv,optstring,options,&long_option_index))!=EOF){
    switch(c){
    case 1:
      /* file name that belongs to current group */
      input_parse(optarg,newgroup);      
      newgroup=0;
      break;
    case 'c':
      /* alternate configuration file */
      configfile=strdup(optarg);
      break;
    case 'g':
      /* start a new file/channel group */
      newgroup=1;
      break;
    case 'h':
      usage(stdout);
      exit(0);
    default:
      usage(stderr);
      exit(0);
    }
  }
}

int look_for_wisdom(char *filename){
  int ret;
  FILE *f=fopen(filename,"r");
  if(!f)return 0;
  ret=fftwf_import_wisdom_from_file(f);
  fclose(f);
 
  if(ret)
    fprintf(stderr,"Found valid postfish-wisdomrc file at %s\n",filename);
  else
    fprintf(stderr,"WARNING: corrupt, invalid or obsolete postfish-wisdomrc file at %s\n",filename);
  return(ret);
}

static int sigill=0;
void sigill_handler(int sig){
  /* make sure */
  if(sig==SIGILL)sigill=1;
}

int main(int argc, char **argv){
  int wisdom=0;

  version=strstr(VERSION,"version.h");
  if(version){
    char *versionend=strchr(version,' ');
    if(versionend)versionend=strchr(versionend+1,' ');
    if(versionend)versionend=strchr(versionend+1,' ');
    if(versionend)versionend=strchr(versionend+1,' ');
    if(versionend){
      int len=versionend-version-9;
      version=strdup(version+10);
      version[len-1]=0;
    }
  }else{
    version="";
  }

  /* parse command line and open all the input files */
  parse_command_line(argc, argv);

  /* We do not care about FPEs; rather, underflow is nominal case, and
     its better to ignore other traps in production than to crash the
     app.  Please inform the FPU of this. */

#ifndef DEBUG
  fedisableexcept(FE_INVALID);
  fedisableexcept(FE_INEXACT);
  fedisableexcept(FE_UNDERFLOW);
  fedisableexcept(FE_OVERFLOW);
#else
  feenableexcept(FE_INVALID);
  feenableexcept(FE_INEXACT);
  feenableexcept(FE_UNDERFLOW);
  feenableexcept(FE_OVERFLOW);
#endif 

  /* Linux Altivec support has a very annoying problem; by default,
     math on denormalized floats will simply crash the program.  FFTW3
     uses Altivec, so boom, but only random booms.
     
     By the C99 spec, the above exception configuration is also
     supposed to handle Altivec config, but doesn't.  So we use the
     below ugliness to both handle altivec and non-alitvec PPC. */

#ifdef __PPC
#include <altivec.h>
  signal(SIGILL,sigill_handler);
  
#if (defined __GNUC__) && (__GNUC__ == 3) && ! (defined __APPLE_CC__)
  __vector unsigned short noTrap = 
    (__vector unsigned short){0,0,0,0,0,0,0x1,0};
#else
  vector unsigned short noTrap = 
    (vector unsigned short)(0,0,0,0,0,0,0x1,0);
#endif

  vec_mtvscr(noTrap);
#endif

  /* check for fftw wisdom file in order:
     ./postfish-wisdomrc
     $(POSTFISHDIR)/postfish-wisdomrc
     ~/.postfish/postfish-wisdomrc
     ETCDIR/postfish-wisdomrc 
     system wisdom */
  

  wisdom=look_for_wisdom("./postfish-wisdomrc");
  if(!wisdom){
    char *rcdir=getenv("POSTFISH_RCDIR");
    if(rcdir){
      char *rcfile="/postfish-wisdomrc";
      char *homerc=calloc(1,strlen(rcdir)+strlen(rcfile)+1);
      strcat(homerc,rcdir);
      strcat(homerc,rcfile);
      wisdom=look_for_wisdom(homerc);
    }
  }
  if(!wisdom){
    char *rcdir=getenv("HOME");
    if(rcdir){
      char *rcfile="/.postfish/postfish-wisdomrc";
      char *homerc=calloc(1,strlen(rcdir)+strlen(rcfile)+1);
      strcat(homerc,rcdir);
      strcat(homerc,rcfile);
      wisdom=look_for_wisdom(homerc);
    }
  }
  if(!wisdom)wisdom=look_for_wisdom(ETCDIR"/postfish-wisdomrc");
  if(!wisdom){
    fftwf_import_system_wisdom(); 
  
    fprintf(stderr,"Postfish could not find the postfish-wisdom configuration file normally built\n"
	    "or installed with Postfish and located in one of the following places:\n"

	    "\t./postfish-wisdomrc\n"
	    "\t$(POSTFISHDIR)/postfish-wisdomrc\n"
	    "\t~/.postfish/postfish-wisdomrc\n\t"
	    ETCDIR"/postfish-wisdomrc\n"
	    "This configuration file is used to reduce the startup time Postfish uses to \n"
	    "pre-calculate Fourier transform tables for the FFTW3 library. Postfish will start\n"
	    "and operate normally, but it will take additional time before popping the main\n"
	    "window because this information must be regenerated each time Postfish runs.\n");
  }

  /* probe outputs */
  if(setvbuf(stdout, NULL, _IONBF , 0))
    fprintf(stderr,"Unable to remove block buffering on stdout; continuing\n");
  
  output_probe_stdout(STDOUT_FILENO);
  output_probe_monitor();

  /* open all the input files */
  if(input_load())exit(1);

  /* load config file */
  if(config_load(configfile))exit(1);

  /* set up filter chains */
  if(declip_load())exit(1);
  if(eq_load(OUTPUT_CHANNELS))exit(1);
  if(deverb_load())exit(1);
  if(multicompand_load(OUTPUT_CHANNELS))exit(1);
  if(singlecomp_load(OUTPUT_CHANNELS))exit(1);
  if(limit_load(OUTPUT_CHANNELS))exit(1);
  if(mute_load())exit(1);
  if(mix_load(OUTPUT_CHANNELS))exit(1);
  if(p_reverb_load())exit(1);

  /* easiest way to inform gtk of changes and not deal with locking
     issues around the UI */
  if(pipe(eventpipe)){
    fprintf(stderr,"Unable to open event pipe:\n"
            "  %s\n",strerror(errno));
    
    exit(1);
  }

  input_seek(0);

  main_looping=0;

  signal(SIGINT,clean_exit);
  signal(SIGSEGV,clean_exit);

  mainpanel_go(argc,argv,input_ch);

  return(0);
}






