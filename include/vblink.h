#include <3ds.h>

void vblink_init(void);
int vblink_open(void);
void vblink_close(void);

extern volatile int vblink_progress;
extern volatile int vblink_error;
extern char vblink_fname[300];
extern Handle vblink_event;