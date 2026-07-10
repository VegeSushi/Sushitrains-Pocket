#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=devkitARM")
endif

include $(DEVKITARM)/gba_rules

#---------------------------------------------------------------------------------
TARGET      :=  sushitrains
BUILD       :=  build
SOURCES     :=  source
DATA        :=  
INCLUDES    :=

#---------------------------------------------------------------------------------
ARCH    :=  -mthumb -mthumb-interwork

CFLAGS  :=  -g -Wall -O3\
            -mcpu=arm7tdmi -mtune=arm7tdmi\
            -fomit-frame-pointer\
            -ffast-math \
            $(ARCH)

CFLAGS  +=  $(INCLUDE)

# ADDED: Essential C++ compiler flags
CXXFLAGS :=  $(CFLAGS) -fno-rtti -fno-exceptions

ASFLAGS :=  $(ARCH)
LDFLAGS =   -g $(ARCH) -Wl,-Map,$(notdir $@).map

#---------------------------------------------------------------------------------
export PATH     :=  $(DEVKITARM)/bin:$(PATH)
LIBS    :=  -lgba
LIBDIRS :=  $(LIBGBA)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------
export OUTPUT   :=  $(CURDIR)/$(TARGET)
export VPATH    :=  $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                    $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR  :=  $(CURDIR)/$(BUILD)

# FIXED: Added 'export' to these variables so the sub-make can see them
export CFILES      :=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export CPPFILES    :=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
export SFILES      :=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
export BINFILES    :=  $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
    export LD   :=  $(CC)
else
    export LD   :=  $(CXX)
endif

export OFILES   := $(addsuffix .o,$(BINFILES)) $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export INCLUDE  :=  $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                    $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                    -I$(CURDIR)/$(BUILD)

export LIBPATHS :=  $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

all : $(BUILD)

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).gba

#---------------------------------------------------------------------------------
else
DEPENDS :=  $(OFILES:.o=.d)

$(OUTPUT).gba   :   $(OUTPUT).elf
$(OUTPUT).elf   :   $(OFILES)

%.o :   %.pcx
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)
endif
#---------------------------------------------------------------------------------