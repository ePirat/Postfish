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

typedef struct reverb_settings {
  sig_atomic_t roomsize; /* 0 - 1000 */
  sig_atomic_t liveness;     /* 0 - 1000 */
  sig_atomic_t hfdamp;   /* 0 - 1000 */
  sig_atomic_t delay;    /* 0 - 1000 */

  sig_atomic_t wet;      /* dB * 10  */
  sig_atomic_t width;    /* 0 - 1000 */
  sig_atomic_t dry_mix;  /* 0, 1 */

  sig_atomic_t active;   /* 0, 1 */
  sig_atomic_t visible;  /* 0, 1 */
} reverb_settings;

typedef struct allpass{
  float *buffer;
  float *bufptr;
  int    size;
} allpass_state;

typedef struct comb{
  float	 filterstore;
  float *buffer;
  float *injptr;
  float *extptr;
  float *extpending;
  int    size;
} comb_state;

#define numcombs 8
#define numallpasses 4
#define scalehfdamp 0.4f
#define scaleliveness 0.4f
#define offsetliveness 0.58f
#define scaleroom 1111
#define stereospread 23
#define fixedgain 0.015f

#define comb0 1116
#define comb1 1188
#define comb2 1277
#define comb3 1356
#define comb4 1422
#define comb5 1491
#define comb6 1557
#define comb7 1617

#define all0 556
#define all1 441
#define all2 341
#define all3 225

/* These values assume 44.1KHz sample rate
   they will probably be OK for 48KHz sample rate
   but would need scaling for 96KHz (or other) sample rates.
   The values were obtained by listening tests. */

static const int combL[numcombs]={
  comb0, comb1, comb2, comb3, 
  comb4, comb5, comb6, comb7 
};

static const int combR[numcombs]={
  comb0+stereospread, comb1+stereospread, comb2+stereospread, comb3+stereospread, 
  comb4+stereospread, comb5+stereospread, comb6+stereospread, comb7+stereospread 
};

static const int allL[numallpasses]={
  all0, all1, all2, all3,
};

static const int allR[numallpasses]={
  all0+stereospread, all1+stereospread, all2+stereospread, all3+stereospread,
};

/* enough storage for L or R */
typedef struct reverb_state {
  comb_state comb[numcombs];
  allpass_state allpass[numallpasses];

  float	bufcomb0[comb0+stereospread];
  float	bufcomb1[comb1+stereospread];
  float	bufcomb2[comb2+stereospread];
  float	bufcomb3[comb3+stereospread];
  float	bufcomb4[comb4+stereospread];
  float	bufcomb5[comb5+stereospread];
  float	bufcomb6[comb6+stereospread];
  float	bufcomb7[comb7+stereospread];
  
  float	bufallpass0[all0+stereospread];
  float	bufallpass1[all1+stereospread];
  float	bufallpass2[all2+stereospread];
  float	bufallpass3[all3+stereospread];

  int energy;
} reverb_state;

typedef struct converted_reverb_settings {
  float feedback;
  float hfdamp;
  int   inject;
  float wet;  
  float wet1;
  float wet2;
  int width;  
  int active;
  int delay;
} converted_reverb_settings;

typedef struct reverb_instance_one{
  int initstate;
  float *cache;
  reverb_state *rL;
  reverb_state *rR;
  converted_reverb_settings sC;
} reverb_instance_one;

typedef struct reverb_instance{
  int    initstate;
  int    ch;
  float *transwindow;
  int    blocksize; /* can go away after lib conversion */
  reverb_instance_one *reverbs;
} reverb_instance;

extern time_linkage *p_reverb_read_master(time_linkage *in);
extern time_linkage *p_reverb_read_channel(time_linkage *in,
					   time_linkage **revA,
					   time_linkage **revB);
extern void p_reverb_reset(void);
extern int p_reverb_load(void);

extern reverb_settings *reverb_channelset; 
extern reverb_settings reverb_masterset; 
