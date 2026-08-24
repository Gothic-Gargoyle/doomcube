.SUFFIXES:
.DEFAULT_GOAL := all

ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment.")
endif

include $(DEVKITPRO)/libogc2/gamecube_rules

#---------------------------------------------------------------------------------
# Version
#---------------------------------------------------------------------------------

VERSION := 0.1.0-dev

GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
GIT_DIRTY := $(shell git diff --quiet --ignore-submodules HEAD 2>/dev/null || echo -dirty)

BUILD_VERSION := $(VERSION)
BUILD_ID := $(GIT_HASH)$(GIT_DIRTY)

DEBUG ?= 1

#---------------------------------------------------------------------------------
# Project
#---------------------------------------------------------------------------------

TARGET      := doomcube-v$(BUILD_VERSION)-$(BUILD_ID)
BUILD       := build
SOURCES     := . source
INCLUDES    :=
LIBDIRS     :=

ISO_DIR := $(CURDIR)/build-iso
ISO_OUT := $(CURDIR)/$(TARGET).iso

GBI_HDR ?= $(CURDIR)/tools/gbi.hdr

MKISOFS := $(shell \
	if command -v genisoimage >/dev/null 2>&1; then \
		command -v genisoimage; \
	elif command -v mkisofs >/dev/null 2>&1; then \
		command -v mkisofs; \
	elif command -v xorriso >/dev/null 2>&1; then \
		echo "xorriso -as mkisofs"; \
	fi \
)

#---------------------------------------------------------------------------------
# Compiler
#---------------------------------------------------------------------------------

CFLAGS := \
	-g \
	-O2 \
	-Wall \
	-DFEATURE_SOUND \
	-DDOOMCUBE_APP_VERSION='"$(BUILD_VERSION)"' \
	-DDOOMCUBE_GIT_ID='"$(BUILD_ID)"' \
	$(MACHDEP) \
	$(INCLUDE)

ifeq ($(DEBUG),1)
CFLAGS += -DDOOMCUBE_DEBUG
endif

CXXFLAGS := $(CFLAGS)

LDFLAGS := \
	-g \
	$(MACHDEP) \
	-Wl,-Map,$(notdir $@).map

#---------------------------------------------------------------------------------
# Libraries
#---------------------------------------------------------------------------------

LIBS := \
	-lSDL2_mixer \
	-lFLAC \
	-lgme \
	-lmpg123 \
	-lvorbisfile \
	-lvorbis \
	-logg \
	-lopusfile \
	-lopus \
	-lwavpack \
	-lxmp \
	-lSDL2 \
	-lopengx \
	-laesnd \
	-liso9660 \
	-logc \
	-lz \
	-lstdc++ \
	-lm

#---------------------------------------------------------------------------------
# Outer build
#---------------------------------------------------------------------------------

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export DEPSDIR := $(CURDIR)/$(BUILD)

export VPATH := \
	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))

#---------------------------------------------------------------------------------
# Sources
#---------------------------------------------------------------------------------

CFILES := \
	dummy.c \
	am_map.c \
	doomdef.c \
	doomstat.c \
	dstrings.c \
	d_event.c \
	d_items.c \
	d_iwad.c \
	d_loop.c \
	d_main.c \
	d_mode.c \
	d_net.c \
	f_finale.c \
	f_wipe.c \
	g_game.c \
	hu_lib.c \
	hu_stuff.c \
	info.c \
	i_cdmus.c \
	i_endoom.c \
	i_joystick.c \
	i_scale.c \
	i_sound.c \
	i_sdlsound.c \
	i_music_gamecube.c \
	mus2mid.c \
	i_system.c \
	i_timer.c \
	memio.c \
	m_argv.c \
	m_bbox.c \
	m_cheat.c \
	m_config.c \
	m_controls.c \
	m_fixed.c \
	m_menu_gamecube.c \
	m_misc.c \
	m_random.c \
	p_ceilng.c \
	p_doors.c \
	p_enemy.c \
	p_floor.c \
	p_inter.c \
	p_lights.c \
	p_map.c \
	p_maputl.c \
	p_mobj.c \
	p_plats.c \
	p_pspr.c \
	p_saveg.c \
	p_setup.c \
	p_sight.c \
	p_spec.c \
	p_switch.c \
	p_telept.c \
	p_tick.c \
	p_user.c \
	r_bsp.c \
	r_data.c \
	r_draw.c \
	r_main.c \
	r_plane.c \
	r_segs.c \
	r_sky.c \
	r_things.c \
	sha1.c \
	sounds.c \
	statdump.c \
	st_lib.c \
	st_stuff.c \
	s_sound.c \
	tables.c \
	v_video.c \
	wi_stuff.c \
	w_checksum.c \
	w_file.c \
	w_main.c \
	w_wad.c \
	w_file_gamecube.c \
	z_zone.c \
	i_input.c \
	i_video.c \
	gc_config.c \
	gc_controls.c \
	gc_launcher.c \
	gc_memcard.c \
	gc_save_stdio.c \
	doomgeneric.c \
	doomgeneric_gamecube.c

CPPFILES := \
	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))

sFILES := \
	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

SFILES := \
	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))

#---------------------------------------------------------------------------------
# Linker
#---------------------------------------------------------------------------------

ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif

#---------------------------------------------------------------------------------
# Objects
#---------------------------------------------------------------------------------

export OFILES := \
	$(CPPFILES:.cpp=.o) \
	$(CFILES:.c=.o) \
	$(sFILES:.s=.o) \
	$(SFILES:.S=.o)

#---------------------------------------------------------------------------------
# Includes
#---------------------------------------------------------------------------------

export INCLUDE := \
	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
	$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
	-I$(CURDIR)/$(BUILD) \
	-I$(CURDIR)/source \
	-I$(DEVKITPRO)/libogc2/gamecube/include \
	-I$(DEVKITPRO)/libogc2/gamecube/include/SDL2 \
	-I$(DEVKITPRO)/portlibs/ppc/include \
	-I$(LIBOGC_INC)

#---------------------------------------------------------------------------------
# Library paths
#---------------------------------------------------------------------------------

export LIBPATHS := \
	-L$(DEVKITPRO)/libogc2/gamecube/lib \
	-L$(DEVKITPRO)/portlibs/ppc/lib \
	-L$(LIBOGC_LIB) \
	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

#---------------------------------------------------------------------------------

.PHONY: all $(BUILD) clean run iso test

all: $(BUILD)

#---------------------------------------------------------------------------------
# Build
#---------------------------------------------------------------------------------

$(BUILD):
	@[ -d $@ ] || mkdir -p $@

	@echo "Building DoomCube v$(BUILD_VERSION) ($(BUILD_ID))"

	@$(MAKE) \
		--no-print-directory \
		-C $(BUILD) \
		-f $(CURDIR)/Makefile \
		$(CURDIR)/$(TARGET).dol

#---------------------------------------------------------------------------------
# Clean
#---------------------------------------------------------------------------------

clean:
	@echo clean ...

	@rm -rf \
		$(BUILD) \
		$(ISO_DIR) \
		$(CURDIR)/$(TARGET).elf \
		$(CURDIR)/$(TARGET).dol \
		$(ISO_OUT)

#---------------------------------------------------------------------------------
# Run on hardware
#---------------------------------------------------------------------------------

run:
	wiiload $(TARGET).dol

#---------------------------------------------------------------------------------
# ISO
#---------------------------------------------------------------------------------

iso:
	@test -n "$(MKISOFS)" || \
		( echo "ERROR: genisoimage, mkisofs or xorriso is required." && false )

	@test -f "$(GBI_HDR)" || \
		( echo "ERROR: missing $(GBI_HDR)" && false )

	@test -f "$(CURDIR)/$(TARGET).dol" || \
		( echo "ERROR: $(TARGET).dol is missing." && false )
	
	@test -f "$(CURDIR)/data/wad/doom1.wad" || \
		test -f "$(CURDIR)/data/wad/doom.wad" || \
		test -f "$(CURDIR)/data/wad/doom2.wad" || \
		test -f "$(CURDIR)/data/wad/tnt.wad" || \
		test -f "$(CURDIR)/data/wad/plutonia.wad" || \
		( echo "ERROR: no supported Doom IWAD found in data/wad/." && false )

	@test -d "$(CURDIR)/data/timidity" || \
		( echo "ERROR: data/timidity is missing." && false )

	@test -f "$(CURDIR)/data/timidity/timidity.cfg" || \
		( echo "ERROR: data/timidity/timidity.cfg is missing." && false )

	@rm -rf "$(ISO_DIR)"

	@mkdir -p "$(ISO_DIR)/timidity"
	@mkdir -p "$(ISO_DIR)/launcher"


	@cp "$(CURDIR)/$(TARGET).dol" "$(ISO_DIR)/bootldr.dol"
	@cp "$(CURDIR)/data/launcher/doomcube.bmp" \
		"$(ISO_DIR)/launcher/doomcube.bmp"

	@for wad in doom1.wad doom.wad doom2.wad tnt.wad plutonia.wad; do \
	if [ -f "$(CURDIR)/data/wad/$$wad" ]; then \
		echo "Adding $$wad"; \
		cp "$(CURDIR)/data/wad/$$wad" "$(ISO_DIR)/$$wad"; \
	fi; \
done
	@echo "Adding TiMidity instrument data"
	@cp -a "$(CURDIR)/data/timidity/." "$(ISO_DIR)/timidity/"
	
	@echo
	@echo "ISO contents:"
	@du -sh "$(ISO_DIR)"
	@echo

	$(MKISOFS) \
		-R \
		-J \
		-G "$(GBI_HDR)" \
		-no-emul-boot \
		-eltorito-platform PPC \
		-b bootldr.dol \
		-o "$(ISO_OUT)" \
		"$(ISO_DIR)"

	@echo
	@echo "Built ISO:"
	@ls -lh "$(ISO_OUT)"
	@echo

#---------------------------------------------------------------------------------
# Dolphin test
#---------------------------------------------------------------------------------

test:
	$(MAKE) clean
	$(MAKE)
	$(MAKE) iso

	@echo
	@echo "Launching DoomCube..."
	@echo

	@echo "DOL:"
	@ls -lh "$(CURDIR)/$(TARGET).dol"
	@echo

	flatpak run \
		--filesystem="$(CURDIR):ro" \
		org.DolphinEmu.dolphin-emu \
		"$(ISO_OUT)"

#---------------------------------------------------------------------------------
# Inner build
#---------------------------------------------------------------------------------

else

DEPENDS := $(OFILES:.o=.d)

.PHONY: all

all: $(OUTPUT).dol

$(OUTPUT).dol: $(OUTPUT).elf

$(OUTPUT).elf: $(OFILES)

#---------------------------------------------------------------------------------
# GameCube save stdio shim
#---------------------------------------------------------------------------------

g_game.o p_saveg.o m_menu_gamecube.o: CFLAGS += \
	-DDOOMCUBE_SAVE_SHIM \
	-include gc_save_stdio.h

-include $(DEPENDS)

endif
