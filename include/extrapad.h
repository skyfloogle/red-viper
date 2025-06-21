#pragma once
#include <3ds/types.h>

typedef struct
{
    s32 _data1[0x10];
    bool recalibrated;
    s32 _data2[0xf];
} extraPadConf;

void extraPadInit(extraPadConf *conf);

bool extraPadLaunch(extraPadConf *conf);
