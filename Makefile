# Project Name
TARGET = TouchPlaited

# Boot mode: QSPI required — Plaits binary will exceed Daisy SRAM 186 KB limit
APP_TYPE = BOOT_QSPI

# Sources: main entry + touch hardware layer + Plaits voice wrapper
CPP_SOURCES = TouchPlaited.cpp \
              $(wildcard touch/*.cpp) \
              synth/plaits_voice.cpp

# Plaits DSP sources (.cc extension — compiled as C++)
# Requires thirdparty/plaits/ and thirdparty/stmlib/ to be populated first.
# See thirdparty/README.md for setup instructions.
CC_SOURCES = $(wildcard thirdparty/plaits/dsp/*.cc) \
             $(wildcard thirdparty/plaits/dsp/chords/*.cc) \
             $(wildcard thirdparty/plaits/dsp/engine/*.cc) \
             $(wildcard thirdparty/plaits/dsp/engine2/*.cc) \
             $(wildcard thirdparty/plaits/dsp/fm/*.cc) \
             $(wildcard thirdparty/plaits/dsp/speech/*.cc) \
             $(wildcard thirdparty/plaits/dsp/physical_modelling/*.cc) \
             $(wildcard thirdparty/stmlib/dsp/*.cc) \
             $(wildcard thirdparty/stmlib/utils/*.cc) \
             thirdparty/plaits/resources.cc

# thirdparty/ is the include root so #include "plaits/dsp/voice.h" and
# #include "stmlib/utils/buffer_allocator.h" resolve correctly
C_INCLUDES += -Ithirdparty

# Library Locations — libDaisy only (DaisySP not needed for Plaits DSP)
LIBDAISY_DIR = lib/libDaisy/

# Core location, and generic Makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

CPP_STANDARD = -std=gnu++17

# Object files for all .cc sources
CC_OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(CC_SOURCES:.cc=.o)))

# Add CC_OBJECTS as explicit prerequisites of the .elf target.
# A rule with no recipe only adds prerequisites — it does not override the existing link recipe.
# This is required because the core Makefile expands $(OBJECTS) in the .elf prerequisites
# at parse time (before our OBJECTS += below runs), so .cc objects would never be built otherwise.
$(BUILD_DIR)/$(TARGET).elf: $(CC_OBJECTS)

# Also append to OBJECTS so the linker recipe's $(OBJECTS) expansion includes the .cc objects
OBJECTS += $(CC_OBJECTS)

# vpath tells make where to find .cc files by basename
vpath %.cc $(sort $(dir $(CC_SOURCES)))

# Compilation rule for .cc -> .o (same flags as the core .cpp rule)
$(BUILD_DIR)/%.o: %.cc Makefile | $(BUILD_DIR)
	$(CXX) -c $(CPPFLAGS) $(CPP_STANDARD) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.cc=.lst)) $< -o $@
