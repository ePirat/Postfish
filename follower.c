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

#include "postfish.h"
#include "follower.h"

static void _analysis(char *base,int seq, float *data, int n,int dB, 
		      off_t offset){

  FILE *f;
  char buf[80];
  sprintf(buf,"%s_%d.m",base,seq);

  f=fopen(buf,"a");
  if(f){
    int i;
    for(i=0;i<n;i++)
      if(dB)
	fprintf(f,"%d %f\n",(int)(i+offset),todB(data[i]));
      else
	fprintf(f,"%d %f\n",(int)(i+offset),(data[i]));

  }
  fclose(f);
}

off_t offset=0;
int offch;

/* Common follower code */

static void prepare_peak(float *peak, float *x, int n, int ahead,
			 int hold, peak_state *ps){
  int ii,jj;
  int loc=ps->loc;
  float val=ps->val;

  /* Although we have two input_size blocks of zeroes after a
     reset, we may still need to look ahead explicitly after a
     reset if the lookahead is exceptionally long */

  if(loc==0 && val==0){
    for(ii=0;ii<ahead;ii++) 
      if((x[ii]*x[ii])>val){
        val=(x[ii]*x[ii]);
        loc=ii+hold;
      }
  }
  
  if(val>peak[0])peak[0]=val;

  for(ii=1;ii<n;ii++){
    if((x[ii+ahead]*x[ii+ahead])>val){
      val=(x[ii+ahead]*x[ii+ahead]);
      loc=ii+ahead+hold;
    }     
    if(ii>=loc){
      /* backfill */
      val=0;
      for(jj=ii+ahead-1;jj>=ii;jj--){
        if((x[jj]*x[jj])>val)val=(x[jj]*x[jj]);
        if(jj<n && val>peak[jj])peak[jj]=val;
      }
      val=(x[ii+ahead]*x[ii+ahead]);
      loc=ii+ahead+hold;
    }
    if(val>peak[ii])peak[ii]=val; 
  }

  ps->loc=loc-input_size;
  ps->val=val;
}

static void fill_work(float *A, float *B, float *work,
		      int ahead, int hold, int mode, peak_state *ps){
  int k;

  if(mode){
    /* peak mode */

    memset(work,0,sizeof(*work)*input_size);
    
    if(B){
      float bigcache[input_size*2];
      memcpy(bigcache,A,sizeof(*work)*input_size);
      memcpy(bigcache+input_size,B,sizeof(*work)*input_size);
      
      prepare_peak(work, bigcache, input_size, ahead, hold, ps);
    }else{
      prepare_peak(work, A, input_size, ahead, hold, ps);
    }
  }else{
    /* rms mode */
    float *cachea=A+ahead;
    
    if(B){
      float *worka=work+input_size-ahead;
      
      for(k=0;k<input_size-ahead;k++)
	work[k]=cachea[k]*cachea[k];
      
      for(k=0;k<ahead;k++)
	worka[k]=B[k]*B[k];    
    }else{

      for(k=0;k<input_size;k++)
	work[k]=cachea[k]*cachea[k];
      
    }
  }

}

void bi_compand(float *A,float *B,float *adj,
		float corner,
		float multiplier,
		float currmultiplier,
		float lookahead, 
		int mode,int softknee,
		iir_filter *attack, iir_filter *decay,
		iir_state *iir, peak_state *ps,
		int active,
		int over){
  
  float work[input_size];
  float kneelevel=fromdB(corner*2);
  int hold,ahead=(mode?step_ahead(attack->alpha):impulse_ahead2(attack->alpha));
  
  if(ahead>input_size)ahead=input_size;
  hold=ahead*(1.-lookahead);
  ahead*=lookahead;
  
  fill_work(A,B,work,ahead,hold,mode,ps);
  
  multiplier*=.5;
  currmultiplier*=.5;
  
  if(!active || !adj)adj=work;

  if(over){
    if(multiplier!=currmultiplier){
      if(softknee){
	compute_iir_over_soft_del(work, input_size, iir, attack, decay, 
				  kneelevel, multiplier, currmultiplier, adj);
      }else{
	compute_iir_over_hard_del(work, input_size, iir, attack, decay, 
				  kneelevel, multiplier, currmultiplier, adj);
      }
    }else{
      if(softknee){
	compute_iir_over_soft(work, input_size, iir, attack, decay, 
			      kneelevel, multiplier, adj);
      }else{
	compute_iir_over_hard(work, input_size, iir, attack, decay, 
			      kneelevel, multiplier, adj);
      }
    }
  }else{
    if(multiplier!=currmultiplier){
      if(softknee){
	compute_iir_under_soft_del(work, input_size, iir, attack, decay, 
				   kneelevel, multiplier, currmultiplier, adj);
      }else{
	compute_iir_under_hard_del(work, input_size, iir, attack, decay, 
				 kneelevel, multiplier, currmultiplier, adj);
      }
    }else{
      if(softknee){
	compute_iir_under_soft(work, input_size, iir, attack, decay, 
			       kneelevel, multiplier, adj);
      }else{
	compute_iir_under_hard(work, input_size, iir, attack, decay, 
			       kneelevel, multiplier, adj);
      }
    }
  }

}

void full_compand(float *A,float *B,float *adj,
		  float multiplier,float currmultiplier,
		  int mode,
		  iir_filter *attack, iir_filter *decay,
		  iir_state *iir, peak_state *ps,
		  int active){
  
  int k;
  float work[input_size];
  int ahead=(mode?step_ahead(attack->alpha):impulse_ahead2(attack->alpha));
  
  fill_work(A,B,work,ahead,0,mode,ps);
  
  multiplier*=.5;
  currmultiplier*=.5;
  
  compute_iir_symmetric_limited(work, input_size, iir, attack, decay);
  
  if(active){
    if(multiplier!=currmultiplier){
      float multiplier_add=(currmultiplier-multiplier)/input_size;
      
      for(k=0;k<input_size;k++){
	adj[k]-=(todB_a2(work[k])+adj[k])*multiplier;
	multiplier+=multiplier_add;
      }
    }else{ 
      for(k=0;k<input_size;k++)
	adj[k]-=(todB_a2(work[k])+adj[k])*multiplier;
    }
  }
}
