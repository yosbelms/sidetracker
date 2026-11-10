ROOT := ..
CORE := $(ROOT)/core
GTEST := $(ROOT)/util/3rdparty/googletest
SANITIZE ?= address,undefined
STEREO ?= 0
OUT ?= $(ROOT)/build/host-tests$(if $(SANITIZE),,-release)$(if $(filter 1,$(STEREO)),-stereo)

CPPFLAGS += -DTRACKER_DESKTOP -isystem $(GTEST)/include -I$(GTEST) -I. \
    $(addprefix -I$(CORE)/,config include source libs/Audio libs/Audio/utility libs/ElapsedMillis AudioEngine FileManager) \
    -I$(CORE)/effector -I$(ROOT)/util/include -I$(ROOT)/util/3rdparty
CXXFLAGS += -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter -MMD -MP -pthread
ifeq ($(STEREO),1)
CPPFLAGS += -DPLATFORM_IMX
endif
LDLIBS += -pthread
ifneq ($(SANITIZE),)
CXXFLAGS += -fno-omit-frame-pointer -fsanitize=$(SANITIZE) -fno-sanitize-recover=all
LDFLAGS += -fsanitize=$(SANITIZE)
endif

# Add portable production modules here. Every *_test.cpp is discovered below.
CORE_SOURCES := $(addprefix libs/Audio/,AudioStream.cpp filter_variable.cpp amplifier.cpp \
    monoToStereo.cpp dspApplyGain.cpp mixer.cpp effect_bitDepth.cpp) \
    source/wavHeaderReader.cpp source/SdStream.cpp source/util_string.cpp source/scales.cpp \
    source/mtEnvelopeGenerator.cpp source/mtWavetableEnvelopes.cpp source/util_math.cpp \
    libs/ElapsedMillis/elapsedMillis.cpp AudioEngine/padToBeatNoteConverter.cpp AudioEngine/SampleFxPreview.cpp AudioEngine/PerfFx/PerfFx.cpp \
    FileManager/instrumentFileValidation.cpp effector/SampleFxChain.cpp effector/SampleFxRealtime.cpp effector/ptfx_validation.cpp \
    $(addprefix effector/,mtDataCopyier.cpp mtEffectModular.cpp mtEffectAmplifier.cpp mtEffectNormalizer.cpp \
    mtEffectDelay.cpp mtEffectEQ.cpp mtEffectChorus.cpp mtEffectFlanger.cpp \
    mtEffectBitcrusher.cpp mtEffectFadeIn.cpp mtEffectFadeOut.cpp mtEffectCompresor.cpp \
    mtEffectLimiter.cpp mtEffectOverdrive.cpp)
CORE_SOURCES += $(patsubst $(CORE)/%,%,$(shell find $(CORE)/effector/palette -name '*.cpp'))
CORE_OBJECTS := $(addprefix $(OUT)/core/,$(CORE_SOURCES:.cpp=.o))
TEST_SOURCES := $(wildcard *_test.cpp)
TEST_BINS := $(patsubst %.cpp,$(OUT)/%,$(TEST_SOURCES))
COMMON_OBJECTS := $(OUT)/test_main.o $(OUT)/gtest-all.o

.DEFAULT_GOAL := test
.PHONY: test run build clean bench bench-m4 bench-m4-build perf-gate

test run: build
	@set -e; for test in $(TEST_BINS); do CE_FIXTURES=$(CURDIR)/fixtures $$test $(TEST_ARGS); done
	python3 -m unittest discover -s . -p '*_test.py'

build: $(TEST_BINS)

$(OUT)/libcore.a: $(CORE_OBJECTS) $(OUT)/data_waveforms.o
	$(AR) rcs $@ $^

$(OUT)/data_waveforms.o: $(CORE)/libs/Audio/data_waveforms.c
	@mkdir -p $(@D)
	$(CC) -c $< -o $@

$(OUT)/%_test: $(OUT)/%_test.o $(COMMON_OBJECTS) $(OUT)/libcore.a
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(OUT)/core/%.o: $(CORE)/%.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(OUT)/%.o: %.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(OUT)/gtest-all.o: $(GTEST)/src/gtest-all.cc Makefile
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OUT)

BENCH_OUT := $(ROOT)/build/bench
BENCH_SOURCES := $(addprefix $(CORE)/libs/Audio/,AudioStream.cpp filter_variable.cpp \
    amplifier.cpp monoToStereo.cpp dspApplyGain.cpp mixer.cpp effect_bitDepth.cpp)
BENCH_HEADERS := $(wildcard $(CORE)/libs/Audio/*.h $(CORE)/libs/Audio/utility/*.h)
BENCH_INCLUDES := -I. $(addprefix -I$(CORE)/,config include source libs/Audio \
    libs/Audio/utility libs/ElapsedMillis) -I$(ROOT)/util/include -I$(ROOT)/util/3rdparty

$(BENCH_OUT)/audio_bench: audio_bench.cpp audio_test_support.h $(BENCH_SOURCES) $(BENCH_HEADERS) Makefile
	@mkdir -p $(@D)
	$(CXX) -std=c++20 -O2 -g -DTRACKER_DESKTOP $(BENCH_INCLUDES) audio_bench.cpp $(BENCH_SOURCES) -o $@

bench: $(BENCH_OUT)/audio_bench
	$<

ARM_PREFIX ?= $(if $(wildcard $(ROOT)/agt/10.3/bin/arm-none-eabi-g++),$(ROOT)/agt/10.3/bin/arm-none-eabi-,arm-none-eabi-)
M4_FLAGS := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -O2 \
    -ffunction-sections -fdata-sections

$(BENCH_OUT)/audio_bench_m4.elf: audio_bench.cpp audio_test_support.h $(BENCH_SOURCES) $(BENCH_HEADERS) bench_m4/startup.c bench_m4/mps2.ld Makefile
	@mkdir -p $(@D)
	$(ARM_PREFIX)gcc $(M4_FLAGS) -c bench_m4/startup.c -o $(BENCH_OUT)/startup.o
	$(ARM_PREFIX)g++ -std=gnu++20 $(M4_FLAGS) -fno-exceptions -fno-rtti \
	    -DKINETISK -DCE_BENCH_M4 $(BENCH_INCLUDES) audio_bench.cpp $(BENCH_SOURCES) \
	    $(BENCH_OUT)/startup.o -T bench_m4/mps2.ld -nostartfiles --specs=nano.specs \
	    --specs=rdimon.specs -Wl,--gc-sections -o $@

bench-m4-build: $(BENCH_OUT)/audio_bench_m4.elf

bench-m4: bench-m4-build
	qemu-system-arm -M mps2-an386 -nographic -icount shift=0 \
	    -semihosting-config enable=on,target=native -kernel $(BENCH_OUT)/audio_bench_m4.elf

perf-gate: $(BENCH_OUT)/audio_bench
	@mkdir -p $(ROOT)/build/perf
	@$(BENCH_OUT)/audio_bench > $(ROOT)/build/perf/host.jsonl
	@set -e; m4_args=""; \
	if command -v qemu-system-arm >/dev/null 2>&1 && command -v $(ARM_PREFIX)g++ >/dev/null 2>&1; then \
	    $(MAKE) --no-print-directory -s bench-m4 > $(ROOT)/build/perf/m4.jsonl; \
	    m4_args="--m4 $(ROOT)/build/perf/m4.jsonl"; \
	else echo "Cortex-M4 instruction check skipped. Install QEMU and an ARM toolchain to enable it."; fi; \
	python3 $(ROOT)/tools/check_perf.py --baseline perf_baseline.json \
	    --host $(ROOT)/build/perf/host.jsonl $$m4_args \
	    $(if $(HOST_MAX_BLOCK_US),--max-block-us $(HOST_MAX_BLOCK_US),)

# Keep test objects for incremental builds, including automatically found tests.
.SECONDARY:
-include $(CORE_OBJECTS:.o=.d) $(COMMON_OBJECTS:.o=.d) $(patsubst %.cpp,$(OUT)/%.d,$(TEST_SOURCES))

CPPFLAGS += -I$(CORE)/effector/palette/vendor/clouds

PALETTE_SOURCES := $(shell find $(CORE)/effector/palette -name '*.cpp')
PALETTE_HEADERS := $(shell find $(CORE)/effector/palette -name '*.h')
PALETTE_INCLUDES := -I$(CORE)/effector -I$(CORE)/effector/palette/vendor/clouds

$(BENCH_OUT)/palette_bench: palette_bench.cpp $(PALETTE_SOURCES) $(PALETTE_HEADERS) $(OUT)/data_waveforms.o
	@mkdir -p $(@D)
	$(CXX) -std=c++20 -O2 $(PALETTE_INCLUDES) palette_bench.cpp $(PALETTE_SOURCES) $(OUT)/data_waveforms.o -o $@

palette-bench: $(BENCH_OUT)/palette_bench
	$<

$(BENCH_OUT)/palette_bench_m4.elf: palette_bench.cpp $(PALETTE_SOURCES) $(PALETTE_HEADERS) bench_m4/startup.c bench_m4/mps2.ld
	@mkdir -p $(@D)
	$(ARM_PREFIX)gcc $(M4_FLAGS) -c bench_m4/startup.c -o $(BENCH_OUT)/palette_startup.o
	$(ARM_PREFIX)gcc $(M4_FLAGS) -c $(CORE)/libs/Audio/data_waveforms.c -o $(BENCH_OUT)/palette_waveforms.o
	$(ARM_PREFIX)g++ -std=gnu++20 $(M4_FLAGS) -fno-exceptions -fno-rtti -DCE_BENCH_M4 $(PALETTE_INCLUDES) \
		palette_bench.cpp $(PALETTE_SOURCES) $(BENCH_OUT)/palette_startup.o $(BENCH_OUT)/palette_waveforms.o \
		-T bench_m4/mps2.ld -nostartfiles --specs=nano.specs --specs=rdimon.specs -Wl,--gc-sections -o $@

palette-bench-m4: $(BENCH_OUT)/palette_bench_m4.elf
	qemu-system-arm -M mps2-an386 -nographic -icount shift=0 -semihosting-config enable=on,target=native -kernel $<

palette-bench-m4-build: $(BENCH_OUT)/palette_bench_m4.elf

.PHONY: palette-bench palette-bench-m4 palette-bench-m4-build
