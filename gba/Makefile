.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/gba_rules

#---------------------------------------------------------------------------------
# GBA multiboot payload
#---------------------------------------------------------------------------------

TARGET   := doomcube_gba_mb
BUILD    := build
SOURCES  := source
INCLUDES :=

ARCH := \
	-mthumb \
	-mthumb-interwork

CFLAGS := \
	-g \
	-Wall \
	-O2 \
	$(ARCH) \
	$(INCLUDE)

CXXFLAGS := $(CFLAGS)

LDFLAGS := \
	-g \
	$(ARCH) \
	-Wl,-Map,$(notdir $@).map

LIBS := \
	-lgba

LIBDIRS := \
	$(LIBGBA)

#---------------------------------------------------------------------------------
# Outer build
#---------------------------------------------------------------------------------

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)

export VPATH := \
	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))

export DEPSDIR := \
	$(CURDIR)/$(BUILD)

CFILES := \
	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))

CPPFILES := \
	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))

sFILES := \
	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

SFILES := \
	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))

ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif

export OFILES := \
	$(CPPFILES:.cpp=.o) \
	$(CFILES:.c=.o) \
	$(sFILES:.s=.o) \
	$(SFILES:.S=.o)

export INCLUDE := \
	$(foreach dir,$(INCLUDES),-iquote $(CURDIR)/$(dir)) \
	$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
	-I$(CURDIR)/$(BUILD)

export LIBPATHS := \
	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: all $(BUILD) clean

#---------------------------------------------------------------------------------

all: $(BUILD)

#---------------------------------------------------------------------------------

$(BUILD):
	@[ -d $@ ] || mkdir -p $@

	@$(MAKE) \
		--no-print-directory \
		-C $(BUILD) \
		-f $(CURDIR)/Makefile \
		$(CURDIR)/$(TARGET).gba

#---------------------------------------------------------------------------------

clean:
	@echo clean GBA payload ...

	@rm -rf \
		$(BUILD) \
		$(TARGET).elf \
		$(TARGET).gba

#---------------------------------------------------------------------------------
# Inner build
#---------------------------------------------------------------------------------

else

DEPENDS := $(OFILES:.o=.d)

.PHONY: all

# Important:
# The recursive build now has an explicit default target.
all: $(OUTPUT).gba

$(OUTPUT).gba: $(OUTPUT).elf

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif