
#include "vb_dsp.h"
#include "v810_mem.h"

bool tileVisible[2048];
int blankTile;

int videoProcessingTime(void) {
	int time = 54688;
	WORLD *worlds = (WORLD*)(V810_DISPLAY_RAM.pmemory + 0x3d800);
	int object_group_id = 3;
	for (int wrld = 31; wrld >= 0; wrld--) {
		if (worlds[wrld].head & 0x40) {
			// END
			time += 308;
			break;
		}
		if ((worlds[wrld].head & 0xc000) == 0) {
			// dummy world
			time += 561;
			continue;
		}
		if ((worlds[wrld].head & 0x3000) != 0x3000) {
			// background world
			int w = (worlds[wrld].w & 0x1fff) + 1;
			int h = worlds[wrld].h + 1;
			int gy = worlds[wrld].gy;
			int mx = worlds[wrld].mx & 0xfff;
			s16 mp = (worlds[wrld].mp << 1) >> 1;
			int my = (worlds[wrld].my << 3) >> 3;

			if (gy > 0) time += (gy < 28 ? gy : 28) * 5;

			switch (worlds[wrld].head & 0x3000) {
				case 0x0000: {
					// normal world
					time += 880;
					int wstart = mx - abs(mp);
					int wend = mx + abs(mp) + w + 1;
					int wtiles = (((wend + 7) & ~7) - (wstart & ~7)) >> 3;
					int offset = (gy - my) & 7;
					for (int y = 0; y < 224; y += 8) {
						if (gy + h + 1 <= y) break;
						if (y == 216) time -= 9;
						if (gy >= y + 8) {
							time += 4 + (y != 216);
							continue;
						}

						bool start = gy >= y;
						bool end = gy + h + 1 <= y + 8;
						time += start ? 12 : (end ? 13 : 16);
						if (y == 0 && !start) time += 6 - 2 * !end;

						int rows;
						if (gy + h + 1 < y + 8 && !start) {
							rows = (gy + h + 1) & 7;
						} else {
							rows = y + 8 - gy;
							if (rows > 8) rows = 8;
						}
						time += 2 * rows * wtiles;
						int tileloads = 1 + (
							offset != 0 &&
							(!start || gy < y + offset) &&
							(!end || start || gy + h + 1 > y + offset)
						);
						time += tileloads * (91 + 2 * wtiles);
					}
					break;
				}
				case 0x1000: {
					// h-bias world
					s16 *params = (s16 *)(&V810_DISPLAY_RAM.pmemory[0x20000 + worlds[wrld].param * 2]);
					time += 880;
					for (int y = 0; y < 224; y += 8) {
						if (gy + h + 1 <= y) break;
						if (y == 216) time -= 9;
						if (gy >= y + 8) {
							time += 4 + (y != 216);
							continue;
						}

						bool start = gy >= y;
						bool end = gy + h + 1 <= y + 8;
						time += start ? 12 : (end ? 13 : 16);
						if (y == 0 && !start) time += 6 - 2 * !end;

						for (int yy = y; yy < y + 8; yy++) {
							if (yy < gy) continue;
							if (!start && yy >= gy + h + 1) break;

							// account for hardware flaw that ors, rather than adds
							int hofstl = params[(y - gy) * 2];
							int hofstr = params[((y - gy) * 2) | 1];

							int left = mx - mp - hofstl;
							int right = mx + mp + hofstr;
							int wstart = left < right ? left : right;
							int wend = (left > right ? left : right) + w + 1;
							int wtiles = (((wend + 7) & ~7) - (wstart & ~7)) >> 3;
							time += 98 + 4 * wtiles;
						}
					}
					break;
				}
				case 0x2000: {
					// affine world
					time += 908;
					for (int y = 0; y < 224; y += 8) {
						if (gy + h + 1 <= y) break;
						if (y == 216) time -= 12;
						if (gy >= y + 8) {
							time += 5 + 2 * (y == 216);
							continue;
						}

						bool start = gy >= y;
						bool end = gy + h + 1 <= y + 8;

						time += start ? 13 : 14;

						if (y == 0 && !start) time += 5;
						if (y == 216 && gy + h + 1 > y + 8) time += 3;

						int startrow = gy;
						if (startrow < y) startrow = y;
						else if (startrow > y + 8) startrow = y + 8;

						int endrow = gy + h + 1;
						if (endrow < y) endrow = y;
						else if (endrow > y + 8) endrow = y + 8;

						int rows = endrow - startrow;
						time += rows * (80 + 4 * (w + 1));
					}
					break;
				}
			}
		} else {
			// object world
			time += 757;
			if (object_group_id < 0) {
				object_group_id = 3;
				time += 28896;
			}
			int start_index = object_group_id == 0 ? 1023 : (tVIPREG.SPT[object_group_id - 1]) & 1023;
			int i = tVIPREG.SPT[object_group_id] & 1023;
			do {
				u8 *obj_y_ptr = (u8 *)(&V810_DISPLAY_RAM.pmemory[0x0003E004 + 8 * i]);
				int y = *obj_y_ptr;
				if (y > 0xf0) time += 27 + 43 + 5 + 2 * ((y + 8) & 0xff);
				else if (y >= 0xe0) time += 28;
				else if (y > 0xd8) time += 27 + 43 + 2 * (0xe0 - y);
				else if ((y & 7) == 0) time += 27 + 43 + 2 * 8;
				else time += 26 + 43 + 5 + 43 + 2 * 8;
			} while (i = (i - 1) & 1023, i != start_index);
		}
	}
	return time;
}