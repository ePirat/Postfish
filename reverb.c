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

/* The plate reverb implementation in this file is originally the work
   of Steve Harris, released under the GPL2 as part of the SWH plugins
   package.  In the interests of proper attribution, the 'AUTHORS' file
   from SWH is reproduced here:

   "[Authors] In no particular order:

    Steve Harris - general stuff
    Frank Neumann - documentation, proofreading, DSP code
    Juhana Sadeharju - DSP code
    Joern Nettingsmeier - DSP code, bug reports and inspiration
    Mark Knecht - testesting, docuementation
    Pascal Haakmat - bugfixes, testing
    Marcus Andersson - DSP code
    Paul Winkler - documentation
    Matthias Nagorni - testing, inspiration
    Nathaniel Virgo - bugfixes
    Patrick Shirkey - testing, inspiration

    Project maintainted by Steve Harris, Southampton UK.
    steve@plugin.org.uk or swh@ecs.soton.ac.uk
    
    Plugin website at http://plugin.org.uk/" */

#include "postfish.h"
#include "reverb.h"

typedef struct {
  int size;
  float *buffer[2];
  int ptr;
  int dptr;
  int dptr_pending;
  int delay;
  int delay_pending;
  float fc;
  float lp[2];
  float a1a;
  float a1b;
  float zm1[2];
} waveguide_nl;

typedef struct {
  float *outbuffer;
  waveguide_nl w[8];
  int prevwet;
  int initstate;
} plate;

typedef struct {
  time_linkage out;
  time_linkage outA;
  time_linkage outB;

  plate *plates;
  int *prevactive;
  int fillstate;
} plate_state;

extern int input_size;
extern int input_ch;

extern float *frame_window;
plate_set *plate_channel_set;
plate_set plate_master_set;

static plate_state channel;
static plate_state master;

/* linear waveguide model */

static void waveguide_init(waveguide_nl *wg,
			   int size, float fc, float da, float db){
  wg->size = size;
  wg->delay_pending = size;
  wg->delay = -1;
  wg->buffer[0] = calloc(size, sizeof(float));
  wg->buffer[1] = calloc(size, sizeof(float));
  wg->ptr = 0;
  wg->dptr = 0;
  wg->dptr_pending = 0;
  wg->fc = fc;
  wg->lp[0] = 0.0f;
  wg->lp[1] = 0.0f;
  wg->zm1[0] = 0.0f;
  wg->zm1[1] = 0.0f;
  wg->a1a = (1.0f - da) / (1.0f + da);
  wg->a1b = (1.0f - db) / (1.0f + db);

}

static void waveguide_nl_reset(waveguide_nl *wg){
  memset(wg->buffer[0], 0, wg->size * sizeof(**wg->buffer));
  memset(wg->buffer[1], 0, wg->size * sizeof(**wg->buffer));
  wg->lp[0] = 0.0f;
  wg->lp[1] = 0.0f;
  wg->zm1[0] = 0.0f;
  wg->zm1[1] = 0.0f;

  wg->ptr=0;
  wg->dptr = wg->delay;
  wg->dptr_pending = wg->delay_pending;
}

static void waveguide_nl_set_delay(waveguide_nl *wg, int delay){
  if (delay > wg->size) {
    wg->delay_pending = wg->size;
  } else if (delay < 1) {
    wg->delay_pending = 1;
  } else {
    wg->delay_pending = delay;
  }

  wg->dptr_pending = wg->ptr + wg->delay_pending;
  if(wg->dptr_pending >= wg->size) wg->dptr_pending -= wg->size;
}

static void waveguide_nl_set_fc(waveguide_nl *wg, float fc){
  wg->fc = fc;
}

static void waveguide_nl_process_lin(waveguide_nl *wg, float in0, float in1, 
				     float *out0, float *out1, int i){
  float tmp;

  *out0 = wg->buffer[0][wg->dptr];
  *out0 = wg->lp[0] * (wg->fc - 1.0f) + wg->fc * *out0;
  wg->lp[0] = *out0;
  tmp = *out0 * -(wg->a1a) + wg->zm1[0];
  wg->zm1[0] = tmp * wg->a1a + *out0;
  *out0 = tmp;
  
  *out1 = wg->buffer[1][wg->dptr];
  *out1 = wg->lp[1] * (wg->fc - 1.0f) + wg->fc * *out1;
  wg->lp[1] = *out1;
  tmp = *out1 * -(wg->a1a) + wg->zm1[1];
  wg->zm1[1] = tmp * wg->a1a + *out1;
  *out1 = tmp;
  
  wg->buffer[0][wg->ptr] = in0;
  wg->buffer[1][wg->ptr] = in1;
  wg->ptr--;
  wg->dptr--;
  if (wg->ptr < 0) wg->ptr += wg->size;
  if (wg->dptr < 0) wg->dptr += wg->size;
}

static void waveguide_nl_process_trans(waveguide_nl *wg, float in0, float in1, 
				       float *out0, float *out1, int i){
  float tmp;

  *out0 = wg->buffer[0][wg->dptr]*(1.-frame_window[i]) +
    wg->buffer[0][wg->dptr_pending]*frame_window[i];
  *out0 = wg->lp[0] * (wg->fc - 1.0f) + wg->fc * *out0;
  wg->lp[0] = *out0;
  tmp = *out0 * -(wg->a1a) + wg->zm1[0];
  wg->zm1[0] = tmp * wg->a1a + *out0;
  *out0 = tmp;
  
  *out1 = wg->buffer[1][wg->dptr]*(1.-frame_window[i]) +
    wg->buffer[1][wg->dptr_pending]*frame_window[i];
  *out1 = wg->lp[1] * (wg->fc - 1.0f) + wg->fc * *out1;
  wg->lp[1] = *out1;
  tmp = *out1 * -(wg->a1a) + wg->zm1[1];
  wg->zm1[1] = tmp * wg->a1a + *out1;
  *out1 = tmp;
  
  wg->buffer[0][wg->ptr] = in0;
  wg->buffer[1][wg->ptr] = in1;
  wg->ptr--;
  wg->dptr--;
  wg->dptr_pending--;
  if (wg->ptr < 0) wg->ptr += wg->size;
  if (wg->dptr < 0) wg->dptr += wg->size;
  if (wg->dptr_pending < 0) wg->dptr_pending += wg->size;
}

/* model the plate reverb as a set of eight linear waveguides */

#define LP_INNER 0.96f
#define LP_OUTER 0.983f

#define RUN_WG(n, junct_a, junct_b, i) process(&w[n], junct_a - out[n*2+1], junct_b - out[n*2], out+n*2, out+n*2+1, i)

static void plate_reset_helper(plate_state *ps){
  ps->fillstate=0;
}

void plate_reset(void){
  plate_reset_helper(&master);
  plate_reset_helper(&channel);
}

static void plate_init(plate *p){
  memset(p,0,sizeof(*p));
  p->outbuffer = calloc(32, sizeof(*p->outbuffer));

  waveguide_init(&p->w[0], 2389, LP_INNER, 0.04f, 0.0f);
  waveguide_init(&p->w[1], 4742, LP_INNER, 0.17f, 0.0f);
  waveguide_init(&p->w[2], 4623, LP_INNER, 0.52f, 0.0f);
  waveguide_init(&p->w[3], 2142, LP_INNER, 0.48f, 0.0f);
  waveguide_init(&p->w[4], 5597, LP_OUTER, 0.32f, 0.0f);
  waveguide_init(&p->w[5], 3692, LP_OUTER, 0.89f, 0.0f);
  waveguide_init(&p->w[6], 5611, LP_OUTER, 0.28f, 0.0f);
  waveguide_init(&p->w[7], 3703, LP_OUTER, 0.29f, 0.0f);

}
 
int plate_load(int outch){
  int i;

  /* setting storage */
  memset(&plate_master_set,0,sizeof(plate_master_set));
  plate_channel_set=calloc(input_ch,sizeof(*plate_channel_set));

  /* output linkages for master and channels */

  master.out.channels=outch;
  master.out.data=malloc(outch*sizeof(*master.out.data));
  for(i=0;i<outch;i++)
    master.out.data[i]=malloc(input_size*sizeof(**master.out.data));

  channel.out.channels=input_ch;
  channel.outA.channels=input_ch;
  channel.outB.channels=input_ch;
  channel.out.data=malloc(input_ch*sizeof(*channel.out.data));
  channel.outA.data=malloc(input_ch*sizeof(*channel.outA.data));
  channel.outB.data=malloc(input_ch*sizeof(*channel.outB.data));
  for(i=0;i<input_ch;i++){
    channel.out.data[i]=malloc(input_size*sizeof(**master.out.data));
    channel.outA.data[i]=malloc(input_size*sizeof(**master.outA.data));
    channel.outB.data[i]=malloc(input_size*sizeof(**master.outB.data));
  }

  /* allocate/initialize plates */
  channel.plates=calloc(input_ch,sizeof(*channel.plates));
  for(i=0;i<input_ch;i++)
    plate_init(&channel.plates[i]);
  
  master.plates=calloc(outch,sizeof(*master.plates));
  for(i=0;i<outch;i++)
    plate_init(&master.plates[i]);

  channel.prevactive=calloc(input_ch,sizeof(*channel.prevactive));
  master.prevactive=calloc(1,sizeof(*master.prevactive));
  return 0;
}
 
static void plate_compute(plate_set *p, plate *ps, float *in, 
			  float *outA, float *outB, float *outC,
			  int count){

  waveguide_nl *w = ps->w;
  float *out=ps->outbuffer;

  void (*process)(waveguide_nl *, float, float, float*, float*, int)=
    waveguide_nl_process_lin;
  
  int pos;
  const float scale = powf(p->time*.0009999f, 1.34f);
  const float lpscale = 1.0f - p->damping*.001f * 0.93f;
  int curwet=p->wet;
  float wet;
  float wet_e;
  
  if(ps->initstate==0){
    wet = fromdB(curwet*.1f);
    wet_e = 1.;
    ps->initstate=1;
  }else{
    wet = fromdB(ps->prevwet*.1f);
    wet_e = fromdB((curwet-ps->prevwet)*.1f/count);
  }

  /* waveguide reconfig */
  for (pos=0; pos<8; pos++) 
    waveguide_nl_set_delay(&w[pos], w[pos].size * scale);  
  for (pos=0; pos<4; pos++) 
    waveguide_nl_set_fc(&w[pos], LP_INNER * lpscale);
  for (; pos<8; pos++) 
    waveguide_nl_set_fc(&w[pos], LP_OUTER * lpscale);

  if(w[0].delay!=w[0].delay_pending)
    process=waveguide_nl_process_trans;

  for (pos = 0; pos < count; pos++) {
    const float alpha = (out[0] + out[2] + out[4] + out[6]) * 0.5f + 
      (in?in[pos]:0.f);
    const float beta = (out[1] + out[9] + out[14]) * 0.666666666f;
    const float gamma = (out[3] + out[8] + out[11]) * 0.666666666f;
    const float delta = (out[5] + out[10] + out[13]) * 0.666666666f;
    const float epsilon = (out[7] + out[12] + out[15]) * 0.666666666f;
    
    RUN_WG(0, beta, alpha, pos);
    RUN_WG(1, gamma, alpha, pos);
    RUN_WG(2, delta, alpha, pos);
    RUN_WG(3, epsilon, alpha, pos);
    RUN_WG(4, beta, gamma, pos);
    RUN_WG(5, gamma, delta, pos);
    RUN_WG(6, delta, epsilon, pos);
    RUN_WG(7, epsilon, beta, pos);
    
    if(!outB || !outC){
      outA[pos]=  beta * wet;
    }else{
      outB[pos]= beta * wet;
      outC[pos]= gamma * wet;
    }
    wet*=wet_e;
  }
  ps->prevwet=curwet;

  if(w[0].delay!=w[0].delay_pending){
    int i;
    for(i=0;i<8;i++){
      w[i].delay=w[i].delay_pending;
      w[i].dptr=w[i].dptr_pending;
    }
  }
}

time_linkage *plate_read_channel(time_linkage *in,
				 time_linkage **revA,
				 time_linkage **revB){
  int i,j,ch=in->channels;
  plate_state *ps=&channel;
  int active[ch];
  
  *revA=&ps->outA;
  *revB=&ps->outB;

  if(in->samples==0){
    ps->out.samples=0;
    ps->outA.samples=0;
    ps->outB.samples=0;
    return &ps->out;
  }

  for(i=0;i<ch;i++)
    active[i]=plate_channel_set[i].panel_active;

  ps->outA.active=0;
  ps->outB.active=0;

  if(ps->fillstate==0){
    for(i=0;i<ch;i++)
      ps->prevactive[i]=active[i];
  }

  for(i=0;i<ch;i++){
    if(active[i] || ps->prevactive[i]){
      float *x=in->data[i];
      float *y=ps->out.data[i];
      float *yA=ps->outA.data[i];
      float *yB=ps->outB.data[i];
      
      /* clear the waveguides of old state if they were inactive */
      if (!ps->prevactive[i] || !ps->fillstate){
	for (j = 0; j < 8; j++) 
	  waveguide_nl_reset(&ps->plates[i].w[j]);
	ps->plates[i].initstate=0;
	memset(ps->plates[i].outbuffer,0,
	       32*sizeof(*ps->plates[i].outbuffer));
      }

      /* process this plate */
      if(mute_channel_muted(in->active,i)){
	plate_compute(&plate_channel_set[i], &ps->plates[i], 
		      0,0,yA,yB,input_size);
      }else{
	
	/* state transition?  If so, the input needs to be smoothed to
           avoid a step transition ringing through the plate */
	if(!ps->fillstate || 
	   !active[i] ||
	   !ps->prevactive[i]){
	  
	  memcpy(y,x,sizeof(*x)*input_size);
	  if(!active[i]){
	    /* transition to inactive */
	    for(j=0;j<input_size;j++)
	      x[j] *= (1.f - frame_window[j]);
	    
	  }else if (!ps->prevactive[i] || !ps->fillstate){
	    /* transition to active */
	    for(j=0;j<input_size;j++)
	      x[j]*= frame_window[j];
	  }
	}else{
	  ps->out.data[i]=x;
	  in->data[i]=y;
	}

	plate_compute(&plate_channel_set[i], &ps->plates[i], 
		      x,0,yA,yB,input_size);


      }

      if(!active[i]){
	/* transition to inactive */
	for(j=0;j<input_size;j++){
	  yA[j] *= (1.f - frame_window[j]);
	  yB[j] *= (1.f - frame_window[j]);
	}
      }
	
      ps->outA.active |= 1<<i;
      ps->outB.active |= 1<<i;

    }else{
      /* fully inactive */
      float *temp=ps->out.data[i];
      ps->out.data[i]=in->data[i];
      in->data[i]=temp;
    }
    ps->prevactive[i]=active[i];
  }

  ps->out.active=in->active;
  ps->out.samples=in->samples;
  ps->outA.samples=in->samples;
  ps->outB.samples=in->samples;
  ps->fillstate=1;
  return &ps->out;
}

time_linkage *plate_read_master(time_linkage *in){
  int i,j,ch=in->channels;
  plate_state *ps=&master;
  int active=plate_master_set.panel_active;

  if(in->samples==0){
    ps->out.samples=0;
    return &ps->out;
  }

  if(ps->fillstate==0){
    ps->prevactive[0]=active;
  }

  for(i=0;i<ch;i++){
    if(active || ps->prevactive[0]){
      float *x=in->data[i];
      float *y=ps->out.data[i];
      
      /* clear the waveguides of old state if they were inactive */
      if (!ps->prevactive[0] || !ps->fillstate){
	for (j = 0; j < 8; j++) 
	  waveguide_nl_reset(&ps->plates[i].w[j]);
	memset(ps->plates[i].outbuffer,0,
	       32*sizeof(*ps->plates[i].outbuffer));
      }

      /* process this plate */
      if(mute_channel_muted(in->active,i)){
	plate_compute(&plate_master_set, &ps->plates[i], 
		      0,y,0,0,input_size);
      }else{

	memcpy(y,x,sizeof(*x)*input_size);

	/* must be done to the input, else the transition impulse
           will ring through the plate */
	if(!active){
	  /* transition to inactive */
	  for(j=0;j<input_size;j++)
	    y[j] *= (1.f - frame_window[j]);
	  
	}else if (!ps->prevactive[0] || !ps->fillstate){
	  /* transition to active */
	  for(j=0;j<input_size;j++)
	    y[j]*= frame_window[j];
	}
	
	plate_compute(&plate_master_set, &ps->plates[i], 
		      y,y,0,0,input_size);
      }

      if(!active){
	/* transition to inactive */
	for(j=0;j<input_size;j++)
	  y[j]*= 1.f - frame_window[j];

      }
      if(!mute_channel_muted(in->active,i))
	for(j=0;j<input_size;j++)
	  y[j]+=x[j];
	
      ps->out.active |= 1<<i;

    }else{
      /* fully inactive */
      float *temp=ps->out.data[i];
      ps->out.data[i]=in->data[i];
      in->data[i]=temp;
      ps->out.active |= in->active & (1<<i);
    }
  }
  ps->prevactive[0]=active;

  ps->out.samples=in->samples;
  ps->fillstate=1;
  return &ps->out;
}
