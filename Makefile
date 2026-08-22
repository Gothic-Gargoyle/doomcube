.SUFFIXES:

ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPRO)/libogc2/gamecube_rules

TARGET      := doomcube
BUILD       := build
SOURCES     := . source
INCLUDES    :=
LIBDIRS     :=

EMBED_WAD ?= 0

ifeq ($(EMBED_WAD),1)
	DATA := data
else
	DATA :=
endif

#---------------------------------------------------------------------------------
# Sound
#---------------------------------------------------------------------------------

CFLAGS = \
	-g \
	-O2 \
	-Wall \
	-DFEATURE_SOUND \
	$(MACHDEP) \
	$(INCLUDE)

CXXFLAGS = $(CFLAGS)

LDFLAGS = \
	-g \
	$(MACHDEP) \
	-Wl,-Map,$(notdir $@).map

ifeq ($(EMBED_WAD),1)
	LDFLAGS += -Wl,--wrap=M_FileExists
endif

#---------------------------------------------------------------------------------
# SDL2 only.
#
# NO SDL2_mixer.
#
# SDL2's GameCube audio driver uses AESND underneath.
#---------------------------------------------------------------------------------

LIBS := \
	-lSDL2 \
	-lopengx \
	-laesnd \
	-logc \
	-lm

#---------------------------------------------------------------------------------
# Outer build
#---------------------------------------------------------------------------------

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)

export VPATH := \
	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
	$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR := $(CURDIR)/$(BUILD)

#---------------------------------------------------------------------------------
# Doom sources
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
	i_sound_gamecube.c \
	i_system.c \
	i_timer.c \
	memio.c \
	m_argv.c \
	m_bbox.c \
	m_cheat.c \
	m_config.c \
	m_controls.c \
	m_fixed.c \
	m_menu.c \
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
	z_zone.c \
	i_input.c \
	i_video.c \
	doomgeneric.c \
	doomgeneric_gamecube.c

#---------------------------------------------------------------------------------
# WAD backend
#---------------------------------------------------------------------------------

ifeq ($(EMBED_WAD),1)
	CFILES += w_file_embedded.c
else
	CFILES += w_file_stdc.c
endif

CPPFILES := \
	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))

sFILES := \
	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

SFILES := \
	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))

BINFILES := \
	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

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

export OFILES_BIN := \
	$(addsuffix .o,$(BINFILES))

export OFILES_SOURCES := \
	$(CPPFILES:.cpp=.o) \
	$(CFILES:.c=.o) \
	$(sFILES:.s=.o) \
	$(SFILES:.S=.o)

export OFILES := \
	$(OFILES_BIN) \
	$(OFILES_SOURCES)

export HFILES := \
	$(addsuffix .h,$(subst .,_,$(BINFILES)))

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
	-I$(LIBOGC_INC)

#---------------------------------------------------------------------------------
# Libraries
#---------------------------------------------------------------------------------

export LIBPATHS := \
	-L$(DEVKITPRO)/libogc2/gamecube/lib \
	-L$(LIBOGC_LIB) \
	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

#---------------------------------------------------------------------------------

.PHONY: \
	$(BUILD) \
	clean \
	run \
	test

#---------------------------------------------------------------------------------

$(BUILD):
	@[ -d $@ ] || mkdir -p $@

	@$(MAKE) \
		--no-print-directory \
		-C $(BUILD) \
		-f $(CURDIR)/Makefile \
		EMBED_WAD=$(EMBED_WAD)

#---------------------------------------------------------------------------------

clean:
	@echo clean ...
	@rm -fr \
		$(BUILD) \
		$(OUTPUT).elf \
		$(OUTPUT).dol

#---------------------------------------------------------------------------------

run:
	wiiload $(TARGET).dol

#---------------------------------------------------------------------------------
# Dolphin development build
#---------------------------------------------------------------------------------

test:
	$(MAKE) clean
	$(MAKE) EMBED_WAD=1

	/usr/bin/flatpak run \
		--filesystem="$(CURDIR):ro" \
		--branch=stable \
		--arch=x86_64 \
		--command=/app/bin/dolphin-emu-wrapper \
		org.DolphinEmu.dolphin-emu \
		"$(CURDIR)/$(TARGET).dol"

#---------------------------------------------------------------------------------
# Inner build
#---------------------------------------------------------------------------------

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).dol: $(OUTPUT).elf

$(OUTPUT).elf: $(OFILES)

$(OFILES_SOURCES): $(HFILES)

#---------------------------------------------------------------------------------
# Embedded WAD for Dolphin
#---------------------------------------------------------------------------------

%_wad.h %.wad.o : %.wad
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------

-include $(DEPENDS)

endif