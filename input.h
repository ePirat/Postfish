extern void input_Acursor_set(off_t c);
extern void input_Bcursor_set(off_t c);
extern off_t input_cursor_get();
extern off_t input_time_to_cursor(char *t);
extern void input_cursor_to_time(off_t cursor,char *t);
extern void time_fix(char *buffer);
extern int input_seek(off_t pos);
extern time_linkage *input_read(void);
extern int input_load(int n,char *list[]);
