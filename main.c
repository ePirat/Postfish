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

pthread_mutex_t master_mutex=PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

int outfileno=-1;
int eventpipe[2];

int main(int argc, char **argv){
  int configfd=-1;

  /* parse command line and open all the input files */
  if(input_load(argc-1,argv+1))exit(1);
  /* set up filter chains */
  if(declip_load())exit(1);
  if(eq_load())exit(1);
  if(suppress_load())exit(1);
  if(multicompand_load())exit(1);
  if(singlecomp_load())exit(1);
  if(limit_load())exit(1);
  if(mute_load())exit(1);

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





