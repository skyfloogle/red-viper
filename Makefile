VERSION_MAJOR := 1
VERSION_MINOR := 2
VERSION_MICRO := 4
VERSION := v${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_MICRO}

#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# GRAPHICS is a list of directories containing graphics files
# GFXBUILD is the directory where converted graphics files will be placed
#   If set to $(BUILD), it will statically link in the converted
#   files as if they were data files.
#
# NO_SMDH: if set to anything, no SMDH file is generated.
# ROMFS is the directory which contains the RomFS, relative to the Makefile (Optional)
# APP_TITLE is the name of the app stored in the SMDH file (Optional)
# APP_DESCRIPTION is the description of the app stored in the SMDH file (Optional)
# APP_AUTHOR is the author of the app stored in the SMDH file (Optional)
# ICON is the filename of the icon (.png), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.png
#     - icon.png
#     - <libctru folder>/default_icon.png
#---------------------------------------------------------------------------------
TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
SOURCES		:=	source/common source/arm source/3ds source/common/inih source/3ds/yattlib-3d/src
DATA		:=	data
INCLUDES	:=	include source/common/inih source/3ds/yattlib-3d/include
GRAPHICS	:=	gfx gfx/maps
GFXBUILD	:=	$(BUILD)
ROMFS		:=	romfs
#GFXBUILD	:=	$(ROMFS)/gfx

include $(TOPDIR)/resources/AppInfo

APP_TITLE := $(shell echo "$(APP_TITLE)" | cut -c1-128)
APP_DESCRIPTION := $(shell echo "$(APP_DESCRIPTION)" | cut -c1-256)
APP_AUTHOR := $(shell echo "$(APP_AUTHOR)" | cut -c1-128)
APP_PRODUCT_CODE := $(shell echo $(APP_PRODUCT_CODE) | cut -c1-16)
APP_UNIQUE_ID := $(shell echo $(APP_UNIQUE_ID) | cut -c1-7)
APP_ENCRYPTED := $(shell echo $(APP_ENCRYPTED) | cut -c1-5)
APP_SYSTEM_MODE := $(shell echo $(APP_SYSTEM_MODE) | cut -c1-4)
APP_SYSTEM_MODE_EXT := $(shell echo $(APP_SYSTEM_MODE_EXT) | cut -c1-6)
ICON := icon.png

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

GIT_HASH := $(shell git log -1 --pretty=format:"%h")
FULL_VERSION := "$(VERSION) - $(GIT_HASH)"

CFLAGS	:=	-g -Wall -Wno-format-truncation -Werror -O3 -mword-relocations -Wswitch \
			-Wno-unused-variable \
			-ffunction-sections \
			-DVERSION=\"$(FULL_VERSION)\" \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__ $(EXTRA_CFLAGS)

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lcitro2d -lcitro3d -lctru -lm -lminizip -lz

ifeq ($(OS),Windows_NT)
	MAKEROM = $(TOPDIR)/tools/makerom.exe
	BANNERTOOL = $(TOPDIR)/tools/bannertool.exe
else
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Linux)
		MAKEROM = $(TOPDIR)/tools/makerom-linux
		BANNERTOOL = $(TOPDIR)/tools/bannertool-linux
	endif
	ifeq ($(UNAME_S),Darwin)
		MAKEROM = $(TOPDIR)/tools/makerom-mac
		BANNERTOOL = $(TOPDIR)/tools/bannertool-mac
	endif
endif

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(CTRULIB) $(PORTLIBS)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
GFXFILES	:=	$(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export T3XFILES		:=	$(GFXFILES:.t3s=.t3x)

export OFILES_SOURCES 	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o) \
			$(if $(filter $(BUILD),$(GFXBUILD)),$(addsuffix .o,$(T3XFILES)))

export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES	:=	$(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
			$(addsuffix .h,$(subst .,_,$(BINFILES))) \
			$(GFXFILES:.t3s=.h)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS	:=	$(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: release testing debug slowdebug $(BUILD) clean all

#---------------------------------------------------------------------------------
all: release
release:	export EXTRA_CFLAGS := -O3 -DDEBUGLEVEL=0
testing:	export EXTRA_CFLAGS := -O3 -DDEBUGLEVEL=1
debug:		export EXTRA_CFLAGS := -g -O0 -DDEBUGLEVEL=2
slowdebug:	export EXTRA_CFLAGS := -g -O0 -DDEBUGLEVEL=3

release testing debug slowdebug:
	@mkdir -p $(BUILD) $(GFXBUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(TARGET).cia


#---------------------------------------------------------------------------------
$(GFXBUILD)/%.t3x	$(BUILD)/%.h	:	%.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@tex3ds -i $< -H $(BUILD)/$*.h -d $(DEPSDIR)/$*.d -o $(GFXBUILD)/$*.t3x

#---------------------------------------------------------------------------------
else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
ifeq ($(strip $(NO_SMDH)),)
.PHONY: all
all	:	$(OUTPUT).3dsx $(OUTPUT).smdh $(OUTPUT).cia
endif
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf	:	$(OFILES) $(ROMFS_T3XFILES)

banner.bnr: $(TOPDIR)/resources/banner.png $(TOPDIR)/resources/audio.wav
	$(BANNERTOOL) makebanner -i $(TOPDIR)/resources/banner.png -a $(TOPDIR)/resources/audio.wav -o banner.bnr

icon.icn: $(TOPDIR)/icon.png
	$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_TITLE)" -p "$(APP_AUTHOR)" -i $(TOPDIR)/icon.png -o icon.icn

cia.rsf:
	cat $(TOPDIR)/tools/template-cia.rsf | sed 's/{APP_TITLE}/$(APP_TITLE)/' | sed 's/{APP_PRODUCT_CODE}/$(APP_PRODUCT_CODE)/' | sed 's/{APP_UNIQUE_ID}/$(APP_UNIQUE_ID)/' | sed 's/{APP_ENCRYPTED}/$(APP_ENCRYPTED)/' | sed 's/{APP_SYSTEM_MODE}/$(APP_SYSTEM_MODE)/' | sed 's/{APP_SYSTEM_MODE_EXT}/$(APP_SYSTEM_MODE_EXT)/' > cia.rsf

$(OUTPUT).cia: banner.bnr icon.icn cia.rsf $(OUTPUT).elf $(TOPDIR)/$(ROMFS)/*
	$(MAKEROM) -f cia -o $(OUTPUT).cia -rsf cia.rsf -target t -exefslogo -elf $(OUTPUT).elf -icon icon.icn -banner banner.bnr -major ${VERSION_MAJOR} -minor ${VERSION_MINOR} -micro ${VERSION_MICRO}
	@echo "built ... $(notdir $@)"


#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
.PRECIOUS	:	%.t3x
%.t3x.o	%_t3x.h :	%.t3x
#---------------------------------------------------------------------------------
	@$(bin2o)

#---------------------------------------------------------------------------------
# rules for assembling GPU shaders
#---------------------------------------------------------------------------------
define shader-as
	$(eval CURBIN := $*.shbin)
	$(eval DEPSFILE := $(DEPSDIR)/$*.shbin.d)
	echo "$(CURBIN).o: $< $1" > $(DEPSFILE)
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"_end[];" > `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"[];" >> `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u32" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`_size";" >> `(echo $(CURBIN) | tr . _)`.h
	picasso -o $(CURBIN) $1
	bin2s $(CURBIN) | $(AS) -o $*.shbin.o
endef

%.shbin.o %_shbin.h : %.v.pica %.g.pica
	@echo $(notdir $^)
	@$(call shader-as,$^)

%.shbin.o %_shbin.h : %.v.pica
	@echo $(notdir $<)
	@$(call shader-as,$<)

%.shbin.o %_shbin.h : %.shlist
	@echo $(notdir $<)
	@$(call shader-as,$(foreach file,$(shell cat $<),$(dir $<)$(file)))

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
