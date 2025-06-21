// This file handles the Circle Pad Pro calibration applet.
// For other communication, see cpp.c.

#include <string.h>
#include <3ds/types.h>
#include <3ds/services/apt.h>

#include "extrapad.h"

void extraPadInit(extraPadConf *conf)
{
    memset(conf, 0, sizeof(*conf));
    conf->_data1[0x0] = 1;
    conf->_data1[0xa] = -1;
}

bool extraPadLaunch(extraPadConf *conf)
{
    aptLaunchLibraryApplet(APPID_EXTRAPAD, conf, sizeof(*conf), 0);
    return conf->recalibrated;
}
