extern int pull_output_feedback(double *peak,double *rms,int *n);
extern void *playback_thread(void *dummy);
extern void output_halt_playback(void);
extern void output_reset(void);
extern void playback_request_seek(off_t cursor);
