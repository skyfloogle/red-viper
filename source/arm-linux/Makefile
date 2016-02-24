# arm-linux makefile

# Build with 'make' or 'make CC=...'
# FIXME: Make a proper makefile

all: r3Ddragon

SPATH=../common
DPATH=./
IPATH=../../include
VPATH = $(SPATH):$(DPATH)

#Set the OBJ and Link flags, based on Debug and Profile
ifdef DEBUGMODE
# build a debug version
OFLAGS = -g -O0 -ffast-math -fomit-frame-pointer -DDEBUGLEVEL=2
LFLAGS = -lm

else
ifdef PROFILEMODE
# build with profiling information
OFLAGS = -pg -O3 -ffast-math -fomit-frame-pointer -DDEBUGLEVEL=4
LFLAGS = -pg -lm

else
# build a normal optimised version
OFLAGS = -O3 -ffast-math -fomit-frame-pointer -DDEBUGLEVEL=0
LFLAGS = -lm

endif
endif

WFLAGS = -Wall -Wno-unused
CFLAGS = -march=armv6 $(WFLAGS) $(OFLAGS) -I$(SPATH) -I$(DPATH) -I$(IPATH)

r3Ddragon: allegro_compat.o main.o drc_core.o drc_exec.o drc_static.o \
			rom_db.o v810_cpu.o v810_ins.o v810_mem.o vb_dsp.o vb_gui.o \
			vb_set.o vb_sound.o arm_utils.o ini.o
	$(CC) -o $@  $^ $(CFLAGS) $(LFLAGS)

allegro_compat.o: allegro_compat.c
	$(CC) -c $(CFLAGS) $< -o $@

main.o: main.c
	$(CC) -c $(CFLAGS) $< -o $@

drc_core.o: drc_core.c
	$(CC) -c $(CFLAGS) $< -o $@

drc_exec.o: drc_exec.s
	$(CC) -c $(CFLAGS) $< -o $@

drc_static.o: drc_static.s
	$(CC) -c $(CFLAGS) $< -o $@

rom_db.o: rom_db.c
	$(CC) -c $(CFLAGS) $< -o $@

v810_cpu.o: v810_cpu.c
	$(CC) -c $(CFLAGS) $< -o $@

v810_ins.o: v810_ins.c
	$(CC) -c $(CFLAGS) $< -o $@

v810_mem.o: v810_mem.c
	$(CC) -c $(CFLAGS) $< -o $@

vb_dsp.o: vb_dsp.c
	$(CC) -c $(CFLAGS) $< -o $@

vb_gui.o: vb_gui.c
	$(CC) -c $(CFLAGS) $< -o $@

vb_set.o: vb_set.c
	$(CC) -c $(CFLAGS) $< -o $@

vb_sound.o: vb_sound.c
	$(CC) -c $(CFLAGS) $< -o $@

arm_utils.o: arm_utils.c
	$(CC) -c $(CFLAGS) $< -o $@

ini.o: inih/ini.c
	$(CC) -Iinih -c $(CFLAGS) $< -o $@

clean:
	rm -f *.o
	rm -f r3Ddragon
