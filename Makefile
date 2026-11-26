#---------------------------------------------------------------------------------
# SW2SNES for hbmenu (Switch 2 homebrew, kaiHEN): devkitPro + ounce-devkit.
#   export DEVKITPRO=/opt/devkitpro && make
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif
TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libounce/switch2_rules

APP_TITLE := NS2xSNES9x
APP_AUTHOR := blueshockz47 (based on Rodpad)
APP_VERSION := 1.0.0-Stable

TARGET := NS2xSNES9x
BUILD		:=	build
S9X		:=	third_party/snes9x
LC		:=	$(S9X)/libretro/libretro-common
SOURCES		:=	source native $(S9X) $(S9X)/apu $(S9X)/apu/bapu/dsp $(S9X)/apu/bapu/smp $(LC)/compat $(LC)/encodings $(LC)/file $(LC)/streams $(LC)/string $(LC)/time $(LC)/vfs
DATA		:=	data
INCLUDES	:=	source $(S9X) $(S9X)/apu $(S9X)/apu/bapu $(S9X)/libretro $(LC)/include
ROMFS		:=	romfs
ICON		:=	icon.jpg

ARCH	:=	$(SWITCH2_CPU) -mtp=soft -fPIE
CFLAGS	:=	-g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES) -Wno-unused-function
CFLAGS	+=	$(INCLUDE) -D__SWITCH__ -DNDEBUG -D__LIBRETRO__ -DALLOW_CPU_OVERCLOCK -DAPP_VERSION=\"$(APP_VERSION)\" -I$(PORTLIBS)/include/freetype2
CXXFLAGS	:= $(CFLAGS) -std=gnu++17
ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS	:= -lEGL -lGLESv2 -lglapi -ldrm_nouveau -lfreetype -lharfbuzz -lpng16 -ljpeg -lbz2 -lminizip -lz -lnx
LIBDIRS	:= $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)
# Snes9x: the files the libretro build compiles (other .cpp files in apu/ are
# #included by these); libretro-common: only the parts the core uses.
CFILES		:=	c4emu.c msu1.c zipfile.c srtc.c obc1.c bsflash.c tile.c bsx.c spc7110.c fxemu.c sdd1.c seta.c sa1hw.c dsp.c \
			compat_posix_string.c compat_strcasestr.c compat_snprintf.c compat_strl.c fopen_utf8.c encoding_utf.c encoding_deflate.c \
			file_path.c file_path_io.c file_stream.c file_stream_transforms.c stdstring.c rtime.c vfs_implementation.c vfs_hybrid.c retro_dirent.c
CPPFILES	:=	$(notdir $(wildcard source/*.cpp)) bridge_snes9x.cpp \
			apu.cpp sdsp.cpp smp.cpp smp_state.cpp cheats2.cpp clip.cpp controls.cpp cpu.cpp cpuexec.cpp cpuops.cpp crosshairs.cpp dma.cpp gfx.cpp \
			globals.cpp loadzip.cpp memmap.cpp ppu.cpp sa1.cpp sa1cpu.cpp snapshot.cpp sha256.cpp bml.cpp fscompat.cpp
export LD	:=	$(CXX)
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export OFILES 	:=	$(OFILES_SRC)
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export APP_ICON := $(TOPDIR)/$(ICON)
export NROFLAGS += --icon=$(APP_ICON) --nacp=$(CURDIR)/$(TARGET).nacp --romfsdir=$(CURDIR)/$(ROMFS)

.PHONY: $(BUILD) clean all
all: $(BUILD)
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf
else
DEPENDS	:=	$(OFILES:.o=.d)
all	:	$(OUTPUT).nro
$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf	:	$(OFILES)
-include $(DEPENDS)
endif
