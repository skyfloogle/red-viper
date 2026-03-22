#include <3ds.h>
#include "utils.h"
#include "vb_dsp.h"
#include "vb_set.h"
#include "cpp.h"
#include "replay.h"
#include "vb_gui.h"
#include "periodic.h"

static volatile bool battery_low = false;
void battery_thread(void) {
	// on citra checking the battery floods the logs
	if (!is_citra) {
		u8 charging, battery_level;
		PTMU_GetBatteryChargeState(&charging);
		PTMU_GetBatteryLevel(&battery_level);
		battery_low = !charging && battery_level <= 2;
	}
}

static bool new_3ds;

void input_init(void) {
	APT_CheckNew3DS(&new_3ds);
	startPeriodic(battery_thread, 20000000, true);
}

extern int arm_keys;
u32 input_state = 0;
static bool new_3ds = false;
// Read the Controller, Fix Me....
HWORD V810_RControll(bool reset) {
	if (replay_playing()) {
		guiGetInput(true);
		return replay_read();
	}

	if (reset) input_state = 0;

    int ret_keys = 0;
    int key = 0;

#ifdef __3DS__
    key = hidKeysHeld();
	if (!new_3ds) {
		key |= cppKeysHeld();
		circlePosition cpp;
		cppCircleRead(&cpp);
		if (cpp.dx >= 41) key |= KEY_CSTICK_RIGHT;
		else if (cpp.dx <= -41) key |= KEY_CSTICK_LEFT;
		if (cpp.dy >= 41) key |= KEY_CSTICK_UP;
		else if (cpp.dy <= -41) key |= KEY_CSTICK_DOWN;
	}

#else
    ret_keys = arm_keys;
    arm_keys = 0;
#endif
    if (battery_low) ret_keys |= VB_BATERY_LOW;
	for (int i = 0; i < 32; i++) {
		int mod = tVBOpt.CUSTOM_MOD[i];
		if (mod == 0) {
			// normal
			input_state &= ~BIT(i);
			input_state |= (key & BIT(i));
		} else if (mod == 1) {
			// toggle
			int down = hidKeysDown();
			input_state ^= down & BIT(i);
		} else if (mod == 2) {
			// turbo
			if (key & BIT(i)) input_state ^= BIT(i);
			else input_state &= ~BIT(i);
		}
		if (input_state & BIT(i)) ret_keys |= vbkey[i];
	}

	if (key & KEY_TOUCH) ret_keys |= guiGetInput(true);

	if ((ret_keys & VB_LPAD_L) && (ret_keys & VB_LPAD_R)) ret_keys &= ~(VB_LPAD_L | VB_LPAD_R);
	if ((ret_keys & VB_LPAD_U) && (ret_keys & VB_LPAD_D)) ret_keys &= ~(VB_LPAD_U | VB_LPAD_D);
	if ((ret_keys & VB_RPAD_L) && (ret_keys & VB_RPAD_R)) ret_keys &= ~(VB_RPAD_L | VB_RPAD_R);
	if ((ret_keys & VB_RPAD_U) && (ret_keys & VB_RPAD_D)) ret_keys &= ~(VB_RPAD_U | VB_RPAD_D);

    ret_keys = ret_keys|0x0002; // Always set bit1, ctrl ID
    return ret_keys;
}