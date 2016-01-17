////////////////////////////////////////////////////////////////
// Defines for the VB Sound system
// put them here because its not very portable (yet)
#ifndef VB_SOUND_H_
#define VB_SOUND_H_

#include <stdbool.h>

//wave data number does not necessarily correspond to channel number
//(value set in Waveform RAM Address)
#define WAVEDATA1 0x01000000 //only 6 bits of 32 bit addressed used
#define WAVEDATA2 0x01000080 //only 6 bits of 32 bit addressed used
#define WAVEDATA3 0x01000100 //only 6 bits of 32 bit addressed used
#define WAVEDATA4 0x01000180 //only 6 bits of 32 bit addressed used
#define WAVEDATA5 0x01000200 //only 6 bits of 32 bit addressed used
#define MODDATA   0x01000280 //only 8 bits of 32 bit addressed used

#define S1INT 0x01000400
#define S1LRV 0x01000404
#define S1FQL 0x01000408
#define S1FQH 0x0100040C
#define S1EV0 0x01000410
#define S1EV1 0x01000414
#define S1RAM 0x01000418

#define S2INT 0x01000440
#define S2LRV 0x01000444
#define S2FQL 0x01000448
#define S2FQH 0x0100044C
#define S2EV0 0x01000450
#define S2EV1 0x01000454
#define S2RAM 0x01000458

#define S3INT 0x01000480
#define S3LRV 0x01000484
#define S3FQL 0x01000488
#define S3FQH 0x0100048C
#define S3EV0 0x01000490
#define S3EV1 0x01000494
#define S3RAM 0x01000498

#define S4INT 0x010004C0
#define S4LRV 0x010004C4
#define S4FQL 0x010004C8
#define S4FQH 0x010004CC
#define S4EV0 0x010004D0
#define S4EV1 0x010004D4
#define S4RAM 0x010004D8

#define S5INT 0x01000500
#define S5LRV 0x01000504
#define S5FQL 0x01000508
#define S5FQH 0x0100050C
#define S5EV0 0x01000510
#define S5EV1 0x01000514
#define S5RAM 0x01000518
#define S5SWP 0x0100051C

#define S6INT 0x01000540
#define S6LRV 0x01000544
#define S6FQL 0x01000548
#define S6FQH 0x0100054C
#define S6EV0 0x01000550
#define S6EV1 0x01000554
#define SSTOP 0x01000580

void sound_init();
void sound_update(int reg);
void sound_close();

#endif //VB_SOUND_H_