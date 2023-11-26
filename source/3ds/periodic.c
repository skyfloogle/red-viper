#include "periodic.h"

#include <3ds.h>

#define THREAD_COUNT 10
static Thread threads[THREAD_COUNT];
static int thread_count = 0;

static volatile bool running = true;

typedef struct PeriodArgs_t {
    void (*func)();
    int periodNanos;
    // to let the main thread know it's safe to delete this struct
    Handle readyEvent;
} PeriodArgs;


static void periodic(void *periodArgs_v) {
    PeriodArgs *periodArgs = (PeriodArgs*)periodArgs_v;
    void (*func)() = periodArgs->func;
    int periodNanos = periodArgs->periodNanos;
    svcSignalEvent(periodArgs->readyEvent);

    u64 lastTime = svcGetSystemTick();
    while (running) {
        u64 newTime = svcGetSystemTick();
        s64 waitNanos = periodNanos - 1000 * (newTime - lastTime) / CPU_TICKS_PER_USEC;
        if (waitNanos > 0)
            svcSleepThread(waitNanos);
        lastTime = newTime;
        func();
    }
}

bool startPeriodic(void (*func)(), int periodNanos) {
    APT_SetAppCpuTimeLimit(30);
    PeriodArgs periodArgs;
    periodArgs.func = func;
    periodArgs.periodNanos = periodNanos;
    svcCreateEvent(&periodArgs.readyEvent, RESET_ONESHOT);
    return (threads[thread_count++] = threadCreate(periodic, &periodArgs, 4000, 0x18, 1, true));
    svcWaitSynchronization(periodArgs.readyEvent, 0);
    svcCloseHandle(periodArgs.readyEvent);
}

void endThreads() {
    running = false;
    for (int i = 0; i < thread_count; i++) threadJoin(threads[i], U64_MAX);
}