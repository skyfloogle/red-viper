#include <stdbool.h>

typedef void (*threadfunc_t)();

bool startPeriodic(threadfunc_t func, int periodNanos);
void endThread(threadfunc_t func);
void endThreads();