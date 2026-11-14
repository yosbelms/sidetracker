ROOT := ../..
CORE := $(ROOT)/core
OUT := $(ROOT)/build/desktop
PRODUCT := ORIGINAL
DESKTOP_MODEL ?= plus
ifeq ($(DESKTOP_MODEL),plus)
PRODUCT := PLUS
BIN := sidetracker-desktop
DESKTOP_CPPFLAGS := -DTRACKER_DESKTOP_PLUS -DPLATFORM_IMX -DFEATURE_SYNTH -DFMT_HEADER_ONLY
DESKTOP_CXXFLAGS := -include PolySynthHostCompatibility.h
SYNTH_HOST := $(wildcard synth/*.cpp)
else ifeq ($(DESKTOP_MODEL),original)
OUT := $(ROOT)/build/desktop-original
BIN := sidetracker-desktop-original
DESKTOP_CPPFLAGS := -DTRACKER_DESKTOP_ORIGINAL
else
$(error Unknown desktop model '$(DESKTOP_MODEL)')
endif
include $(ROOT)/version.mk
DIRS := source libs interface effector FileManager Exporter AudioEngine synth
CPP := $(shell cd $(CORE) && find $(DIRS) -name '*.cpp')
C := $(shell cd $(CORE) && find libs -name '*.c')
INCLUDES := include config source synth libs libs/Audio libs/ElapsedMillis libs/Audio/utility libs/FT812 libs/fonts libs/NewDisplay interface effector FileManager Exporter AudioEngine libs/Audio/Polyverb AudioEngine/ConstantValues AudioEngine/MasterEnhancer
CPPFLAGS += $(FW_VERSION_DEFINES) -DTRACKER_DESKTOP $(DESKTOP_CPPFLAGS) -I. $(addprefix -I$(CORE)/,$(INCLUDES)) -I$(ROOT)/util/include -I$(ROOT)/util/3rdparty -I$(ROOT)/synth/include $(shell pkg-config --cflags sdl2)
CXXFLAGS += -std=c++20 -O2 -MMD -MP $(DESKTOP_CXXFLAGS)
CFLAGS += -O2 -MMD -MP
LDLIBS += $(shell pkg-config --libs sdl2)
OBJS := $(addprefix $(OUT)/core/,$(CPP:.cpp=.o) $(C:.c=.o))
HOST := $(patsubst %.cpp,$(OUT)/%.o,$(wildcard *.cpp) $(SYNTH_HOST))

SD_TARGET ?= sd
all: $(OUT)/$(BIN) $(SD_TARGET)
check: $(OUT)/$(BIN) $(SD_TARGET)
	@set -eu; card=$$(mktemp -d); trap 'rm -rf "$$card"' EXIT; \
	$< --sd "$$card" --screenshot $(OUT)/pattern.bmp --panel; \
	$< --sd "$$card" --tap-button Config --screenshot $(OUT)/config.bmp --panel; \
	$< --sd "$$card" --tap-button Song --screenshot $(OUT)/song.bmp --panel; \
	if cmp -s $(OUT)/pattern.bmp $(OUT)/config.bmp || \
	   cmp -s $(OUT)/pattern.bmp $(OUT)/song.bmp || \
	   cmp -s $(OUT)/config.bmp $(OUT)/song.bmp; then \
	    echo "Expected three different firmware screens" >&2; exit 1; \
	fi
ifeq ($(DESKTOP_MODEL),plus)
	python3 $(ROOT)/tools/desktop_plus_synth_check.py $<
	python3 $(ROOT)/tools/desktop_perf_fx_check.py $<
endif
	python3 $(ROOT)/tools/desktop_master_check.py $<
	python3 $(ROOT)/tools/desktop_fx_chain_check.py $< --output $(OUT)

sd:
	$(MAKE) -C $(ROOT) sd

$(OUT)/$(BIN): $(OBJS) $(HOST)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(OUT)/core/%.o: $(CORE)/%.cpp Makefile $(ROOT)/version.mk
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -include cstdint -include cassert -c $< -o $@

$(OUT)/core/%.o: $(CORE)/%.c Makefile $(ROOT)/version.mk
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OUT)/%.o: %.cpp Makefile $(ROOT)/version.mk
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

-include $(OBJS:.o=.d) $(HOST:.o=.d)
.PHONY: all check sd

CPPFLAGS += -I$(CORE)/effector/stpalette/vendor/clouds
