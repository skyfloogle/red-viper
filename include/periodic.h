#include <stdbool.h>

bool startPeriodic(void (*func)(), int periodNanos);
void endThreads();