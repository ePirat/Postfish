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

typedef struct {
  sig_atomic_t device;
  sig_atomic_t source[OUTPUT_CHANNELS];
  sig_atomic_t bytes;
  sig_atomic_t ch;
  sig_atomic_t format; /* WAV, AIFC, LE, BE */
} output_subsetting;

typedef struct {
  sig_atomic_t panel_active[2];
  sig_atomic_t panel_visible;

  output_subsetting stdout;
  output_subsetting monitor;
} output_settings;

typedef struct {
  int type;
  char *name;
  char *file;
} output_monitor_entry;

extern int output_stdout_available;
extern int output_stdout_device;
extern int output_monitor_available;
extern output_monitor_entry *monitor_list;
extern int monitor_entries;

extern sig_atomic_t master_att;

extern int pull_output_feedback(float *peak,float *rms);
extern void *playback_thread(void *dummy);
extern void output_halt_playback(void);
extern void output_reset(void);
extern void playback_request_seek(off_t cursor);
extern int output_feedback_deep(void);

extern int output_probe_stdout(int outfileno);
extern int output_probe_monitor(void );

extern void outpanel_monitor_off();
extern void outpanel_stdout_off();

extern output_settings outset;
