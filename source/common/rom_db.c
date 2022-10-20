#include <stdio.h>
#include "v810_cpu.h"
#include "vb_types.h"
#include "v810_mem.h"
#include "rom_db.h"

static unsigned long crc_table[256];

ROM_INFO rom_db[] =
{
    { "Unknown",         0x00000000, "N/A"  },

    { "3D Tetris (U)",                   0xBB71B522, "Good"    },
    { "Galactic Pinball (JU)",           0xC9710A36, "Good"    },
    { "Golf (U)",                        0x2199AF41, "Good"    },
    { "Insmouse No Yakata (J)",          0xEFD0AC36, "Bad (1)" },
    { "Jack Bros. (J)",                  0xCAB61E8B, "Good"    },
    { "Jack Bros. (U)",                  0xA44DE03C, "Good"    },
    { "Jack Bros. (U)",                  0x81AF4D6D, "Bad (1)" },
    { "Mario Clash (JU)",                0xA47DE78C, "Good"    },
    { "Mario Clash (JU)",                0xBF0D0AB0, "Bad (1)" },
    { "Mario's Tennis (JU)",             0x7CE7460D, "Good"    },
    { "Mario's Tennis (JU)",             0x5AC0D8BB, "Bad (1)" },
    { "Nester's Funky Bowling (U)",      0xDF4D56B4, "Good"    },
    { "Nester's Funky Bowling (U)",      0x63A181AF, "Bad (1)" },
    { "Nester's Funky Bowling (U)",      0x9C092BCE, "Bad (2)" },
    { "Nester's Funky Bowling (U)",      0x2F76ECA9, "Bad (3)" },
    { "Panic Bomber (J)",                0x40498F5E, "Good"    },
    { "Panic Bomber (U)",                0x19BB2DFB, "Good"    },
    { "Panic Bomber (U)",                0x25FB89BB, "Bad (1)" },
    { "Red Alarm (J)",                   0x7E85C45D, "Good"    },
    { "Red Alarm (U)",                   0xAA10A7B4, "Good"    },
    { "SD Gundam - Dimension War (J)",   0x44788197, "Good"    },
    { "Space Invaders (J)",              0xFA44402D, "Good"    },
    { "Space Squash (J)",                0x60895693, "Good"    },
    { "Space Squash (J)",                0xC2211FCC, "Bad (1)" },
    { "T&E Virtual Golf (J)",            0x6BA07915, "Good"    },
    { "T&E Virtual Golf (J)",            0x41FB63BF, "Bad (1)" },
    { "Teleroboxer (JU)",                0x36103000, "Good"    },
    { "V Tetris (J)",                    0x3CCb67AE, "Good"    },
    { "Vertical Force (J)",              0x9E9B8B92, "Good"    },
    { "Vertical Force (J)",              0x05D06377, "Bad (1)" },
    { "Vertical Force (J)",              0x066288FF, "Bad (2)" },
    { "Vertical Force (U)",              0x4C32BA5E, "Good"    },
    { "Virtual Boy Wario Land (JU)",     0x133E9372, "Good"    },
    { "Virtual Bowling (J)",             0x20688279, "Good"    },
    { "Virtual Fishing (J)",             0x526CC969, "Good"    },
    { "Virtual Fishing (J)",             0x45471E40, "Bad (1)" },
    { "Virtual Fishing (J)",             0xC4ED4B12, "Bad (2)" },
    { "Virtual Lab (J)",                 0x8989FE0A, "Good"    },
    { "Virtual League Baseball (U)",     0x736B40D6, "Good"    },
    { "Virtual League Baseball (U)",     0xCC62AB38, "Bad (1)" },
    { "Virtual League Baseball (U)",     0x2E20B6E7, "Bad (2)" },
    { "Virtual League Baseball (U)",     0xCE830401, "Bad (3)" },
    { "Virtual Pro Yakyuu '95 (J)",      0x9BA8BB5E, "Good"    },
    { "Waterworld (U)",                  0x82A95E51, "Good"    },
    { "Waterworld (U)",                  0x742298D1, "Bad (1)" },

    {{ '\0', 0, '\0' }}
};

int db_find(unsigned long crc32) {
    int i=1;

    while(rom_db[i].crc32 != 0) {
        if(rom_db[i].crc32==crc32) { return i; }
        i++;
    }

    return 0;
}

/* Code by Glenn Rhoads, modified by frostgiant to work with binary files */

void gen_table(void)                /* build the crc table */
{
    unsigned long crc, poly;
    int	i, j;

    poly = 0xEDB88320L;
    for (i = 0; i < 256; i++)
    {
        crc = i;
        for (j = 8; j > 0; j--)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ poly;
            else
                crc >>= 1;
        }
        crc_table[i] = crc;
    }
}


unsigned long get_crc(int romSize)    /* calculate the crc value */
{
    unsigned long crc=0;
    int val=0;
    int i=0;

    crc = 0xFFFFFFFF;
    while (i<romSize) {
        val = V810_ROM1.pmemory[i];
        crc = ((crc>>8) & 0x00FFFFFF) ^ crc_table[ (crc^val) & 0xFF ];
        i++;
    }


    return( crc^0xFFFFFFFF );
}



