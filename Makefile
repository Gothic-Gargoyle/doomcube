.SUFFIXES:
.DEFAULT_GOAL := all

ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment.")
endif

include $(DEVKITPRO)/libogc2/gamecube_rules

#---------------------------------------------------------------------------------
# Version
#---------------------------------------------------------------------------------

BASE_VERSION ?= 0.1.0
VERSION ?= $(BASE_VERSION)-dev
RC ?= 1

GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
GIT_DIRTY := $(shell git diff --quiet --ignore-submodules HEAD 2>/dev/null || echo -dirty)

BUILD_VERSION := $(VERSION)
BUILD_ID := $(GIT_HASH)$(GIT_DIRTY)

# Development builds include normal debug diagnostics by default.
#
# TRACE is reserved for very verbose per-operation diagnostics.
#
# Public RC/final release targets explicitly disable both.
# release-debug deliberately enables both for diagnostic builds.
DEBUG ?= 1
TRACE ?= 0

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

ifeq ($(TRACE),1)
CFLAGS += -DDOOMCUBE_TRACE
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
	gc_dvd_fst.c \
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
# Native GameCube ISO
#---------------------------------------------------------------------------------

iso: all
	@test -f "$(CURDIR)/tools/native-gcm/mkdoomcube.py" || \
		( echo "ERROR: missing native GCM builder." && false )

	@test -f "$(CURDIR)/tools/native-gcm/apploader.c" || \
		( echo "ERROR: missing tools/native-gcm/apploader.c" && false )

	@test -d "$(CURDIR)/build-deps/cubeboot-tools/ppc/apploader" || \
		( echo "ERROR: missing build-deps/cubeboot-tools." && false )

	@echo "Building DoomCube apploader"
	@cp "$(CURDIR)/tools/native-gcm/apploader.c" \
		"$(CURDIR)/build-deps/cubeboot-tools/ppc/apploader/apploader.c"

	@$(MAKE) -C \
		"$(CURDIR)/build-deps/cubeboot-tools/ppc/apploader" \
		clean

	@$(MAKE) -C \
		"$(CURDIR)/build-deps/cubeboot-tools/ppc/apploader"

	@cp \
		"$(CURDIR)/build-deps/cubeboot-tools/ppc/apploader/apploader.bin" \
		"$(CURDIR)/tools/native-gcm/apploader.bin"

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

	@mkdir -p "$(ISO_DIR)/data/wad"
	@mkdir -p "$(ISO_DIR)/data/pwad"
	@mkdir -p "$(ISO_DIR)/data/deh"
	@mkdir -p "$(ISO_DIR)/data/timidity"
	@mkdir -p "$(ISO_DIR)/launcher"

	@cp "$(CURDIR)/data/launcher/doomcube.bmp" \
		"$(ISO_DIR)/launcher/doomcube.bmp"

	@for wad in doom1.wad doom.wad doom2.wad tnt.wad plutonia.wad; do \
		if [ -f "$(CURDIR)/data/wad/$$wad" ]; then \
			echo "Adding $$wad"; \
			cp "$(CURDIR)/data/wad/$$wad" "$(ISO_DIR)/data/wad/$$wad"; \
		fi; \
	done

	@if [ -d "$(CURDIR)/data/pwad" ]; then \
		cp -a "$(CURDIR)/data/pwad/." "$(ISO_DIR)/data/pwad/"; \
	fi

	@if [ -d "$(CURDIR)/data/deh" ]; then \
		cp -a "$(CURDIR)/data/deh/." "$(ISO_DIR)/data/deh/"; \
	fi

	@echo "Adding TiMidity instrument data"
	@cp -a "$(CURDIR)/data/timidity/." "$(ISO_DIR)/data/timidity/"

	@echo
	@echo "Native disc contents:"
	@du -sh "$(ISO_DIR)"
	@echo

	python3 "$(CURDIR)/tools/native-gcm/mkdoomcube.py" \
		--dol "$(CURDIR)/$(TARGET).dol" \
		--apploader "$(CURDIR)/tools/native-gcm/apploader.bin" \
		--root "$(ISO_DIR)" \
		--output "$(ISO_OUT)" \
		--title "DOOMCUBE"

	@echo
	@echo "Built native GameCube ISO:"
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

#---------------------------------------------------------------------------------
# Player release bundle
#---------------------------------------------------------------------------------
# DOOMCUBE_RELEASE_TARGET

DOOMCUBE_RELEASE_DIR := $(CURDIR)/dist
DOOMCUBE_RELEASE_ZIP := $(DOOMCUBE_RELEASE_DIR)/$(TARGET).zip

DOOMCUBE_APPLOADER_SRC := $(CURDIR)/tools/native-gcm/apploader.c
DOOMCUBE_APPLOADER_DIR := $(CURDIR)/build-deps/cubeboot-tools/ppc/apploader
DOOMCUBE_COMMON_DIR    := $(CURDIR)/build-deps/cubeboot-tools/ppc/common
DOOMCUBE_APPLOADER_BIN := $(CURDIR)/tools/native-gcm/apploader.bin

DOOMCUBE_BUNDLE_BUILDER := $(CURDIR)/tools/release/build_bundle.py

.PHONY: rc release release-debug release-bundle

# ------------------------------------------------------------------
# Public build flavours
# ------------------------------------------------------------------
#
# make
#     Development build:
#       VERSION = <base>-dev
#       DEBUG   = 1
#       TRACE   = 0
#
# make rc
#     Release candidate:
#       VERSION = <base>-rc<RC>
#       DEBUG   = 0
#       TRACE   = 0
#
# make release
#     Final production release:
#       VERSION = <base>
#       DEBUG   = 0
#       TRACE   = 0
#
# make release-debug
#     Player-installable diagnostic release:
#       VERSION = <base>-debug
#       DEBUG   = 1
#       TRACE   = 1
#
# Each packaged flavour starts from a clean object tree. This prevents
# objects compiled with one DOOMCUBE_DEBUG setting from leaking into a
# differently configured release.

rc:
	@echo
	@echo "============================================================"
	@echo " DoomCube release candidate $(BASE_VERSION)-rc$(RC)"
	@echo " Production logging"
	@echo "============================================================"
	@echo
	@$(MAKE) clean
	@$(MAKE) \
		DEBUG=0 \
		TRACE=0 \
		VERSION="$(BASE_VERSION)-rc$(RC)" \
		release-bundle

release:
	@echo
	@echo "============================================================"
	@echo " DoomCube release $(BASE_VERSION)"
	@echo " Production logging"
	@echo "============================================================"
	@echo
	@$(MAKE) clean
	@$(MAKE) \
		DEBUG=0 \
		TRACE=0 \
		VERSION="$(BASE_VERSION)" \
		release-bundle

release-debug:
	@echo
	@echo "============================================================"
	@echo " DoomCube diagnostic release $(BASE_VERSION)-debug"
	@echo " Verbose DEBUG/TRACE logging"
	@echo "============================================================"
	@echo
	@$(MAKE) clean
	@$(MAKE) \
		DEBUG=1 \
		TRACE=1 \
		VERSION="$(BASE_VERSION)-debug" \
		release-bundle

# Internal packaging implementation.
#
# Do not call this target for normal release work; use rc, release or
# release-debug so the correct logging configuration and clean rebuild
# are guaranteed.
release-bundle: all
	@echo
	@echo "============================================================"
	@echo " DoomCube player release"
	@echo "============================================================"
	@echo

	@test -f "$(DOOMCUBE_APPLOADER_SRC)" || \
		( echo "ERROR: missing tracked apploader source:"; \
		  echo "  $(DOOMCUBE_APPLOADER_SRC)"; false )

	@test -d "$(DOOMCUBE_APPLOADER_DIR)" || \
		( echo "ERROR: cubeboot-tools apploader directory is missing."; \
		  echo "Run the developer setup first."; false )

	@test -d "$(DOOMCUBE_COMMON_DIR)" || \
		( echo "ERROR: cubeboot-tools common directory is missing."; false )

	@test -f "$(DOOMCUBE_BUNDLE_BUILDER)" || \
		( echo "ERROR: release bundle builder is missing:"; \
		  echo "  $(DOOMCUBE_BUNDLE_BUILDER)"; false )

	@echo "Building DoomCube-owned apploader..."

	@cp \
		"$(DOOMCUBE_APPLOADER_SRC)" \
		"$(DOOMCUBE_APPLOADER_DIR)/apploader.c"

	@$(MAKE) -C "$(DOOMCUBE_COMMON_DIR)"
	@$(MAKE) -C "$(DOOMCUBE_APPLOADER_DIR)" clean
	@$(MAKE) -C "$(DOOMCUBE_APPLOADER_DIR)"

	@test -f "$(DOOMCUBE_APPLOADER_DIR)/apploader.bin" || \
		( echo "ERROR: apploader build did not produce apploader.bin."; false )

	@cp \
		"$(DOOMCUBE_APPLOADER_DIR)/apploader.bin" \
		"$(DOOMCUBE_APPLOADER_BIN)"

	@echo
	@echo "Building player release bundle..."

	@python3 "$(DOOMCUBE_BUNDLE_BUILDER)" \
		--dol "$(CURDIR)/$(TARGET).dol" \
		--apploader "$(DOOMCUBE_APPLOADER_BIN)" \
		--builder "$(CURDIR)/tools/native-gcm/mkdoomcube.py" \
		--packer "$(CURDIR)/tools/release/pack.py" \
		--launcher "$(CURDIR)/data/launcher/doomcube.bmp" \
		--timidity "$(CURDIR)/data/timidity" \
		--dist "$(DOOMCUBE_RELEASE_DIR)" \
		--archive-name "$(TARGET).zip"

	@test -f "$(DOOMCUBE_RELEASE_ZIP)" || \
		( echo "ERROR: release ZIP was not created."; false )

	@echo
	@ls -lh "$(DOOMCUBE_RELEASE_ZIP)"
	@echo
	@echo "$(DOOMCUBE_RELEASE_ZIP)"
	@echo
