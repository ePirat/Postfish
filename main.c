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
#include "postfish.h"
#include <fenv.h>  // Thank you C99!
#include <fftw3.h>
#include "input.h"
#include "output.h"
#include "declip.h"
#include "eq.h"
#include "suppress.h"
#include "multicompand.h"
#include "singlecomp.h"
#include "limit.h"
#include "mute.h"
#include "mix.h"

pthread_mutex_t master_mutex=PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

int outfileno=-1;
int eventpipe[2];

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

int main(int argc, char **argv){
  int configfd=-1;
  int wisdom=0;

  /* We do not care about FPEs; rather, underflow is nominal case, and
     its better to ignore other traps in production than to crash the
     app.  Please inform the FPU of this. */
  fedisableexcept(FE_INEXACT);
  fedisableexcept(FE_UNDERFLOW);
  fedisableexcept(FE_OVERFLOW);

  /* Linux Altivec support has a very annoying problem; by default,
     math on denormalized floats will simply crash the program.  FFTW3
     uses Altivec, so boom.
     
     By the C99 spec, the above exception configuration is also
     supposed to handle Altivec config, but doesn't.  So we use the
     below ugliness. */

#ifdef __PPC
#include <altivec.h>
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

  /* parse command line and open all the input files */
  if(input_load(argc-1,argv+1))exit(1);
  /* set up filter chains */
  if(declip_load())exit(1);
  if(eq_load(OUTPUT_CHANNELS))exit(1);
  if(suppress_load())exit(1);
  if(multicompand_load(OUTPUT_CHANNELS))exit(1);
  if(singlecomp_load(OUTPUT_CHANNELS))exit(1);
  if(limit_load(OUTPUT_CHANNELS))exit(1);
  if(mute_load())exit(1);
  if(mix_load(OUTPUT_CHANNELS))exit(1);

  /* look at stdout... do we have a file or device? */
  if(!isatty(STDOUT_FILENO)){
    /* assume this is the file/device for output */
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

  /* easiest way to inform gtk of changes and not deal with locking
     issues around the UI */
  if(pipe(eventpipe)){
    fprintf(stderr,"Unable to open event pipe:\n"
            "  %s\n",strerror(errno));
    
    exit(1);
  }

  input_seek(0);
  mainpanel_go(argc,argv,input_ch);

  output_halt_playback();

  //save_settings(configfd);
  if(configfd>=0)close(configfd);

  return(0);
}





