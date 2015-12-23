LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := r3Ddragon
LOCAL_SRC_FILES := ../source/common/allegro_compat.c ../source/arm-linux/main.c ../source/common/drc_core.c ../source/common/drc_exec.s ../source/common/drc_static.s \
                   ../source/common/rom_db.c ../source/common/v810_cpu.c ../source/common/v810_ins.c ../source/common/v810_mem.c ../source/common/vb_dsp.c ../source/common/vb_gui.c \
                   ../source/common/vb_set.c ../source/arm-linux/arm_utils.c ../source/common/inih/ini.c
LOCAL_C_INCLUDES := include source/common/inih
TARGET_ARCH     := arm
TARGET_ARCH_ABI := armeabi
TARGET_PLATFORM := android-16

ifeq ($(NDK_DEBUG), 0)
    LOCAL_CFLAGS    := -DDEBUGLEVEL=0
else
    LOCAL_CFLAGS    := -DDEBUGLEVEL=3
endif

include $(BUILD_EXECUTABLE)