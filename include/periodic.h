#include <stdbool.h>

typedef void (*threadfunc_t)(void);

bool startPeriodic(threadfunc_t func, int periodNanos, bool altcpu);
bool startPeriodicVsync(threadfunc_t func);
void endThread(threadfunc_t func);
void endThreads(void);