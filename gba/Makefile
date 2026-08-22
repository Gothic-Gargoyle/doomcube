.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment.")
endif

include $(DEVKITARM)/gba_rules

TARGET  := doomcube_gba_mb
BUILD   := build
SOURCES := source
INCLUDES :=

ARCH := -mthumb -mthumb-interwork

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

LIBS := -lgba

LIBDIRS := $(LIBGBA)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)

export VPATH := \
	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))

export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES := \
	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))

CPPFILES := \
	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))

SFILES := \
	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif

export OFILES := \
	$(CPPFILES:.cpp=.o) \
	$(CFILES:.c=.o) \
	$(SFILES:.s=.o)

export INCLUDE := \
	$(foreach dir,$(INCLUDES),-iquote $(CURDIR)/$(dir)) \
	$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
	-I$(CURDIR)/$(BUILD)

export LIBPATHS := \
	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) \
		--no-print-directory \
		-C $(BUILD) \
		-f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -rf \
		$(BUILD) \
		$(TARGET).elf \
		$(TARGET).gba

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).gba: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif