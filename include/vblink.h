#include <3ds.h>

void vblink_init();
void vblink_thread();

volatile int vblink_progress = -1;
volatile int vblink_error = 0;
int vblink_listenfd = -1;

Handle vblink_event;