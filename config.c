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
#include "config.h"
#include <errno.h>
#include <string.h>
#include <ctype.h>

typedef struct {
  char *key;
  int bank;
  int A;
  int B;
  int C;
  int vals;
  int *vec;
  char *string;
} configentry;

static int configentries=0;
static configentry *config_list=0;

static int look_for_key(char *key,int bank,int A, int B, int C){
  int i;
  for(i=0;i<configentries;i++)
    if(!strcmp(key,config_list[i].key) && 
       config_list[i].bank==bank &&
       config_list[i].A==A &&
       config_list[i].B==B &&
       config_list[i].C==C)return i;
  return -1;
}

/* query the loaded config; this is just an interface to pre-parsed
   input */
const char *config_get_string(char *key,int bank, int A, int B, int C){
  int i=look_for_key(key,bank,A,B,C);
  if(i==-1)return NULL;

  return config_list[i].string;
}

int config_get_integer(char *key,int bank, int A, int B, int C,int valnum, int *val){
  int i=look_for_key(key,bank,A,B,C);
  if(i==-1)return -1;
  if(valnum<config_list[i].vals){
    *val=config_list[i].vec[valnum];
    return 0;
  }
  return -1;
}

int config_get_sigat(char *key,int bank, int A, int B, int C,int valnum, sig_atomic_t *val){
  int ival=*val;
  int ret=config_get_integer(key, bank, A, B, C,valnum, &ival);
  *val=ival;
  return ret;
}

int config_get_vector(char *key,int bank, int A, int B, int C,int n, sig_atomic_t *v){
  int i=look_for_key(key,bank,A,B,C),j;
  if(i==-1)return -1;

  for(j=0;j<n && j<config_list[i].vals;j++)
    v[j]=config_list[i].vec[j];
  return 0;
}

static configentry *old_or_new(char *key,int bank,int A, int B, int C){
  int i=look_for_key(key,bank,A,B,C);

  if(i==-1){
    /* create a new entry */
    i=configentries;
    configentries++;
    if(config_list){
      config_list=realloc(config_list,sizeof(*config_list)*configentries);
      memset(&config_list[i],0,sizeof(*config_list));
    }else{
      config_list=calloc(1,sizeof(*config_list));
    }
    config_list[i].key=strdup(key);
    config_list[i].bank=bank;
    config_list[i].A=A;
    config_list[i].B=B;
    config_list[i].C=C;
  }
  return config_list+i;
}

static void extend_vec(configentry *c,int n){
  if(n>c->vals){
    if(!c->vec)
      c->vec=calloc(n,sizeof(*c->vec));
    else{
      c->vec=realloc(c->vec,n*sizeof(*c->vec));
      memset(c->vec+c->vals,0,(n-c->vals)*sizeof(*c->vec));
    }
    c->vals=n;
  }
}

/* dump changes back into existing local config state; this is mostly
   an elaborate means of meging changes into an existing file that may
   be a superset of what's currently running */
void config_set_string(char *key,int bank, int A, int B, int C, const char *s){
  configentry *c=old_or_new(key,bank,A,B,C);
  c->string=strdup(s);
}

void config_set_integer(char *key,int bank, int A, int B, int C, int valnum, int val){
  configentry *c=old_or_new(key,bank,A,B,C);
  extend_vec(c,valnum+1);
  c->vec[valnum]=val;
}

void config_set_vector(char *key,int bank, int A, int B, int C,int n, sig_atomic_t *v){
  int i;
  configentry *c=old_or_new(key,bank,A,B,C);
  extend_vec(c,n);
  for(i=0;i<n;i++)
    c->vec[i]=v[i];
}

int config_load(char *filename){
  FILE *f=fopen(filename,"r");
  char key[80];
  int bank,A,B,C,width,rev;
  int errflag=0;

  fprintf(stderr,"Loading state configuration file %s... ",filename);

  sprintf(key,"[file beginning]");

  if(!f){
    fprintf(stderr,"No config file %s; will be created on save/exit.\n",filename);
    return 0;
  }

  /* search for magic */
  if(fscanf(f,"Postfish rev %d",&rev)!=1 || rev!=2){
    fprintf(stderr,"File %s is not a postfish state configuration file.\n",filename);
    fclose(f);
    return -1;
  }

  /* load file */
  while(!feof(f)){
    int c=fgetc(f);
    switch(c){
    case '(':  /* string type input */

      if (fscanf(f,"%79s bank%d A%d B%d C%d l%d \"",
		 key,&bank,&A,&B,&C,&width)==6){
	char *buffer=calloc(width+1,sizeof(*buffer));
	for(c=0;c<width;c++)buffer[c]=fgetc(f);

	config_set_string(key,bank,A,B,C,buffer);
	free(buffer);
	fscanf(f,"\" )");
	errflag=0;
      }else{
	if(!errflag){
	  fprintf(stderr,"Configuration file parse error after %s\n",key);
	  errflag=1;
	}
      }

      break;
    case '[':  /* vector type input */
      if (fscanf(f,"%79s bank%d A%d B%d C%d v%d \"",
		 key,&bank,&A,&B,&C,&width)==6){
	int *vec=calloc(width,sizeof(*vec));
	for(c=0;c<width;c++){
	  if(fscanf(f,"%d",vec+c)!=1){
	    if(!errflag){
	      fprintf(stderr,"Configuration file parse error after %s\n",key);
	      errflag=1;
	      break;
	    }
	  }
	}
	fscanf(f," ]");
	
	config_set_vector(key,bank,A,B,C,width,vec);
	free(vec);
	errflag=0;
      }else{
	if(!errflag){
	  fprintf(stderr,"Configuration file parse error after %s\n",key);
	  errflag=1;
	}
      }

      break;
    default:
      /* whitespace OK, other characters indicate a parse error */
      if(!isspace(c) && !errflag && c!=EOF){
	fprintf(stderr,"Configuration file parse error after %s\n",key);
	errflag=1;
      }
      
      break;
    }
  }
  
  fclose(f);
  fprintf(stderr,"done.\n");
  return 0;
}

/* save the config */
void config_save(char *filename){
  int i,j;
  FILE *f=fopen(filename,"w");

  fprintf(stderr,"Saving state to %s ...",filename);

  if(!f){
    fprintf(stderr,"\nUnable to save config file %s: %s\n",filename,strerror(errno));
    return;
  }
  
  fprintf(f,"Postfish rev 2\n");
  
  for(i=0;i<configentries;i++){
    configentry *c=config_list+i;
    if(c->string)
      fprintf(f,"(%s bank%d A%d B%d C%d l%d \"%s\" )\n",
	      c->key,c->bank,c->A,c->B,c->C,strlen(c->string),c->string);
    if(c->vec){
      fprintf(f,"[%s bank%d A%d B%d C%d v%d ",
	      c->key,c->bank,c->A,c->B,c->C,c->vals);
      for(j=0;j<c->vals;j++)
	fprintf(f,"%d ",c->vec[j]);
      
      fprintf(f,"]\n");
    }
  }
  fclose(f);
  fprintf(stderr," done.\n");
}

