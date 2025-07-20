#include "periodic.h"

#include <3ds.h>


#define THREAD_COUNT 10
static Thread threads[THREAD_COUNT];
static volatile bool threadrunning[THREAD_COUNT];
static threadfunc_t threadfuncs[THREAD_COUNT];

typedef struct PeriodArgs_t {
    threadfunc_t func;
    int periodNanos;
    int id;
    // to let the main thread know it's safe to delete this struct
    Handle readyEvent;
} PeriodArgs;


static void periodic(void *periodArgs_v) {
    PeriodArgs *periodArgs = (PeriodArgs*)periodArgs_v;
    threadfunc_t func = periodArgs->func;
    int id = periodArgs->id;
    int periodNanos = periodArgs->periodNanos;
    svcSignalEvent(periodArgs->readyEvent);

    Handle timer;
    svcCreateTimer(&timer, RESET_PULSE);
    svcSetTimer(timer, 0, periodNanos);

    while (threadrunning[id]) {
        svcWaitSynchronization(timer, periodNanos);
        func();
    }

    svcCloseHandle(timer);
}

static void periodic_vsync(void *periodArgs_v) {
    PeriodArgs *periodArgs = (PeriodArgs*)periodArgs_v;
    threadfunc_t func = periodArgs->func;
    int id = periodArgs->id;
    svcSignalEvent(periodArgs->readyEvent);

    while (threadrunning[id]) {
        gspWaitForVBlank();
        func();
    }
}

bool startPeriodic(threadfunc_t func, int periodNanos, bool altcpu) {
    PeriodArgs periodArgs;
    periodArgs.func = func;
    periodArgs.periodNanos = periodNanos;
    svcCreateEvent(&periodArgs.readyEvent, RESET_ONESHOT);
    for (int i = 0; i < THREAD_COUNT; i++) {
        if (!threadrunning[i]) {
            periodArgs.id = i;
            threadfuncs[i] = func;
            threadrunning[i] = true;
            threads[i] = threadCreate(periodic, &periodArgs, 4000, 0x18, altcpu, true);
            if (!threads[i]) {
                threadrunning[i] = false;
                return false;
            }
            svcWaitSynchronization(periodArgs.readyEvent, INT64_MAX);
            svcCloseHandle(periodArgs.readyEvent);
            return true;
        }
    }
    return false;
}

bool startPeriodicVsync(threadfunc_t func) {
    PeriodArgs periodArgs;
    periodArgs.func = func;
    svcCreateEvent(&periodArgs.readyEvent, RESET_ONESHOT);
    for (int i = 0; i < THREAD_COUNT; i++) {
        if (!threadrunning[i]) {
            periodArgs.id = i;
            threadfuncs[i] = func;
            threadrunning[i] = true;
            threads[i] = threadCreate(periodic_vsync, &periodArgs, 4000, 0x18, 0, true);
            if (!threads[i]) {
                threadrunning[i] = false;
                return false;
            }
            svcWaitSynchronization(periodArgs.readyEvent, INT64_MAX);
            svcCloseHandle(periodArgs.readyEvent);
            return true;
        }
    }
    return false;
}

void endThread(threadfunc_t func) {
    for (int i = 0; i < THREAD_COUNT; i++) {
        if (threadrunning[i] && threadfuncs[i] == func) {
            threadrunning[i] = false;
            threadJoin(threads[i], U64_MAX);
        }
    }
}

void endThreads() {
    for (int i = 0; i < THREAD_COUNT; i++) threadrunning[i] = false;
    for (int i = 0; i < THREAD_COUNT; i++) threadJoin(threads[i], U64_MAX);
}