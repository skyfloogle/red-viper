#include <3ds.h>

void vblink_init(void);
void vblink_open(void);
void vblink_close(void);

volatile int vblink_progress = -1;
volatile int vblink_error = 0;
Handle vblink_event;