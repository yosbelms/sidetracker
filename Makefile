# Polyend Tracker Community Edition (CE) - Top-Level Monorepo Makefile

ROOT_DIR      := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ifneq ($(wildcard $(ROOT_DIR)/agt/10.3/bin/arm-none-eabi-gcc),)
  TOOLCHAIN   ?= $(ROOT_DIR)/agt/10.3/bin/arm-none-eabi-
else
  TOOLCHAIN   ?= arm-none-eabi-
endif

NPROC         ?= $(shell nproc 2>/dev/null || echo 4)
CORE_DIR      := $(ROOT_DIR)/core
MK66_DIR      := $(ROOT_DIR)/platforms/mk66
IMX_DIR       := $(ROOT_DIR)/platforms/imx
SYNTH_DIR     := $(ROOT_DIR)/synth
UTIL_DIR      := $(ROOT_DIR)/util
OUT_DIR       := $(ROOT_DIR)/build
SD_DIR        := $(OUT_DIR)/SD
MINI_DONOR    := $(ROOT_DIR)/temp/fmini-1-2.5.12
OBER_DONOR    := $(ROOT_DIR)/temp/obervave-main
SYNTH_PATCH_INPUTS := $(addprefix $(MINI_DONOR)/,LICENSE firmware/src/eng_fmini_presets.h firmware/src/eng_fmini_filter.h) \
                      $(addprefix $(OBER_DONOR)/,LICENSE firmware/src/obv_patches.h firmware/src/obv_params.h)

include $(ROOT_DIR)/version.mk

ORIGINAL_POSTFIX ?= $(ORIGINAL_VER_MAJOR).$(ORIGINAL_VER_MINOR).$(ORIGINAL_VER_PATCH)
PLUS_POSTFIX     ?= $(PLUS_VER_MAJOR).$(PLUS_VER_MINOR).$(PLUS_VER_PATCH)
MINI_POSTFIX     ?= $(MINI_VER_MAJOR).$(MINI_VER_MINOR).$(MINI_VER_PATCH)

ORIGINAL_PTF := $(EDITION_NAME)_$(ORIGINAL_POSTFIX).ptf
PLUS_PTF     := $(EDITION_NAME)Plus_$(PLUS_POSTFIX).ptf
MINI_PTF     := $(EDITION_NAME)Mini_$(MINI_POSTFIX).ptf

RELEASE_VERSION ?= $(ORIGINAL_VER_MAJOR).$(ORIGINAL_VER_MINOR).$(ORIGINAL_VER_PATCH)
RELEASE_ZIP := $(EDITION_NAME)_$(RELEASE_VERSION).zip

.DEFAULT_GOAL := all

.PHONY: all original plus mini desktop desktop-check sd test host-test bench bench-m4 perf-gate ptfx release-zip clean version bump-patch bump-minor

all: original plus mini

sd:
	python3 "$(ROOT_DIR)/tools/generate_stpalette_patches.py" --sd "$(SD_DIR)"
ifeq ($(words $(wildcard $(SYNTH_PATCH_INPUTS))),$(words $(SYNTH_PATCH_INPUTS)))
	python3 "$(ROOT_DIR)/tools/port_analog_synth_presets.py" --sd "$(SD_DIR)"
else
	@echo "MINI and OBER patch sources are unavailable in temp/. Staged the STPalette patches."
endif

desktop: sd
	$(MAKE) -C "$(ROOT_DIR)/platforms/desktop" SD_TARGET=

desktop-check: sd
	$(MAKE) -C "$(ROOT_DIR)/platforms/desktop" check SD_TARGET=

test host-test:
	$(MAKE) -C "$(ROOT_DIR)/tests" test

bench bench-m4 perf-gate:
	$(MAKE) -C "$(ROOT_DIR)/tests" $@

original: sd
	@echo "=================================================================="
	@echo " Building Original Tracker (MK66) - $(EDITION_NAME) $(ORIGINAL_VER_MAJOR).$(ORIGINAL_VER_MINOR).$(ORIGINAL_VER_PATCH) ($(BUILD_TAG))"
	@echo "=================================================================="
	@rm -f "$(MK66_DIR)/build/obj/source/PlatformImpl.o"
	$(MAKE) -C "$(CORE_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		PLATFORM=MK66 PRODUCT=ORIGINAL \
		POLYUTIL_DIR="$(UTIL_DIR)" \
		POLYSYNTH_DIR="$(SYNTH_DIR)"
	$(MAKE) -C "$(MK66_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		TRACKER_DIR="$(CORE_DIR)" \
		PRODUCT=ORIGINAL \
		TARGET_POSTFIX="$(ORIGINAL_POSTFIX)" \
		BUILD_ID_STR="$(BUILD_ID_STR)"
	@mkdir -p "$(SD_DIR)/Firmware"
	@cp "$(MK66_DIR)/build/PolyendTracker_$(ORIGINAL_POSTFIX).ptf" "$(OUT_DIR)/$(ORIGINAL_PTF)"
	@cp "$(OUT_DIR)/$(ORIGINAL_PTF)" "$(SD_DIR)/Firmware/PolyendTracker.ptf"
	@echo "-> Output: $(OUT_DIR)/$(ORIGINAL_PTF)"

plus: sd
	@echo "=================================================================="
	@echo " Building Tracker+ (IMX) - $(EDITION_NAME) $(PLUS_VER_MAJOR).$(PLUS_VER_MINOR).$(PLUS_VER_PATCH) ($(BUILD_TAG))"
	@echo "=================================================================="
	@rm -f "$(IMX_DIR)/build-PLUS/obj/source/PlatformImpl.o"
	$(MAKE) -C "$(CORE_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		PLATFORM=IMX PRODUCT=PLUS \
		POLYUTIL_DIR="$(UTIL_DIR)" \
		POLYSYNTH_DIR="$(SYNTH_DIR)"
	$(MAKE) -C "$(IMX_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		TRACKER_DIR="$(CORE_DIR)" \
		POLYUTIL_DIR="$(UTIL_DIR)" \
		POLYSYNTH_DIR="$(SYNTH_DIR)" \
		PRODUCT=PLUS \
		TARGET_POSTFIX="$(PLUS_POSTFIX)" \
		BUILD_ID_STR="$(BUILD_ID_STR)"
	@mkdir -p "$(SD_DIR)/Firmware"
	@cp "$(IMX_DIR)/build-PLUS/TrackerPlus_$(PLUS_POSTFIX).ptf" "$(OUT_DIR)/$(PLUS_PTF)"
	@cp "$(OUT_DIR)/$(PLUS_PTF)" "$(SD_DIR)/Firmware/TrackerPlus.ptf"
	@echo "-> Output: $(OUT_DIR)/$(PLUS_PTF)"

mini: sd
	@echo "=================================================================="
	@echo " Building Tracker Mini (IMX) - $(EDITION_NAME) $(MINI_VER_MAJOR).$(MINI_VER_MINOR).$(MINI_VER_PATCH) ($(BUILD_TAG))"
	@echo "=================================================================="
	@rm -f "$(IMX_DIR)/build-MINI/obj/source/PlatformImpl.o"
	$(MAKE) -C "$(CORE_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		PLATFORM=IMX PRODUCT=MINI \
		POLYUTIL_DIR="$(UTIL_DIR)" \
		POLYSYNTH_DIR="$(SYNTH_DIR)"
	$(MAKE) -C "$(IMX_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		TRACKER_DIR="$(CORE_DIR)" \
		POLYUTIL_DIR="$(UTIL_DIR)" \
		POLYSYNTH_DIR="$(SYNTH_DIR)" \
		PRODUCT=MINI \
		TARGET_POSTFIX="$(MINI_POSTFIX)" \
		BUILD_ID_STR="$(BUILD_ID_STR)"
	@mkdir -p "$(SD_DIR)/Firmware"
	@cp "$(IMX_DIR)/build-MINI/TrackerMini_$(MINI_POSTFIX).ptf" "$(OUT_DIR)/$(MINI_PTF)"
	@cp "$(OUT_DIR)/$(MINI_PTF)" "$(SD_DIR)/Firmware/TrackerMini.ptf"
	@echo "-> Output: $(OUT_DIR)/$(MINI_PTF)"

ptfx: sd
	@mkdir -p "$(ROOT_DIR)/editor-fx-sdk/build/Effects" "$(SD_DIR)/Effects"
	@set -e; for src in "$(ROOT_DIR)"/editor-fx-sdk/effects/*.c; do \
		base=$$(basename "$$src" .c); \
		python3 "$(ROOT_DIR)/editor-fx-sdk/ptfx_builder.py" "$$src" -o "$(ROOT_DIR)/editor-fx-sdk/build/Effects/$$base.ptfx" --toolchain "$(TOOLCHAIN)"; \
		cp "$(ROOT_DIR)/editor-fx-sdk/build/Effects/$$base.ptfx" "$(SD_DIR)/Effects/"; \
	done

release-zip:
	@test "$(RELEASE_VERSION)" = "$(ORIGINAL_VER_MAJOR).$(ORIGINAL_VER_MINOR).$(ORIGINAL_VER_PATCH)" && \
	      test "$(RELEASE_VERSION)" = "$(PLUS_VER_MAJOR).$(PLUS_VER_MINOR).$(PLUS_VER_PATCH)" && \
	      test "$(RELEASE_VERSION)" = "$(MINI_VER_MAJOR).$(MINI_VER_MINOR).$(MINI_VER_PATCH)" && \
	      test "$(ORIGINAL_POSTFIX):$(PLUS_POSTFIX):$(MINI_POSTFIX)" = "$(RELEASE_VERSION):$(RELEASE_VERSION):$(RELEASE_VERSION)" || \
	      { echo "Release version, model versions, and firmware filename suffixes must match." >&2; exit 1; }
	@mkdir -p "$(OUT_DIR)"
	@set -eu; stage=$$(mktemp -d "$(OUT_DIR)/release-stage.XXXXXX"); \
		trap 'rm -rf "$$stage"' EXIT; \
		$(MAKE) all OUT_DIR="$$stage" SD_DIR="$$stage/SD"; \
		cmp "$$stage/$(ORIGINAL_PTF)" "$$stage/SD/Firmware/PolyendTracker.ptf"; \
		cmp "$$stage/$(PLUS_PTF)" "$$stage/SD/Firmware/TrackerPlus.ptf"; \
		cmp "$$stage/$(MINI_PTF)" "$$stage/SD/Firmware/TrackerMini.ptf"; \
		mkdir "$$stage/Licenses"; \
		cp "$(ROOT_DIR)/LICENSE" "$(ROOT_DIR)/NOTICE.md" "$(SYNTH_DIR)/lib/LICENSE-PROPRIETARY.txt" "$$stage/Licenses/"; \
		if test -d "$$stage/SD/Patches/MINI"; then \
			cp "$(ROOT_DIR)/build/SideTrackerAnalogPresets/"*-LICENSE.txt "$$stage/Licenses/"; \
			cp "$(ROOT_DIR)/build/SideTrackerAnalogPresets/README.txt" "$$stage/Licenses/AnalogPresets.txt"; \
		fi; \
		(cd "$$stage" && zip -q -r "$(RELEASE_ZIP)" "$(ORIGINAL_PTF)" "$(PLUS_PTF)" "$(MINI_PTF)" SD Licenses); \
		mv "$$stage/$(RELEASE_ZIP)" "$(OUT_DIR)/$(RELEASE_ZIP)"
	@echo "-> Output: $(OUT_DIR)/$(RELEASE_ZIP)"

clean:
	$(MAKE) -C "$(CORE_DIR)" clean PLATFORM=MK66 PRODUCT=ORIGINAL
	$(MAKE) -C "$(CORE_DIR)" clean PLATFORM=IMX PRODUCT=PLUS
	$(MAKE) -C "$(CORE_DIR)" clean PLATFORM=IMX PRODUCT=MINI
	$(MAKE) -C "$(MK66_DIR)" clean PRODUCT=ORIGINAL
	$(MAKE) -C "$(IMX_DIR)" clean PRODUCT=PLUS
	$(MAKE) -C "$(IMX_DIR)" clean PRODUCT=MINI
	rm -rf "$(OUT_DIR)" "$(ROOT_DIR)/editor-fx-sdk/build"

version:
	@echo "Edition      : $(EDITION_NAME) (Build $(BUILD_TAG))"
	@echo "Original (MK66): $(EDITION_NAME) $(ORIGINAL_VER_MAJOR).$(ORIGINAL_VER_MINOR).$(ORIGINAL_VER_PATCH) -> $(ORIGINAL_PTF)"
	@echo "Tracker+ (IMX) : $(EDITION_NAME) $(PLUS_VER_MAJOR).$(PLUS_VER_MINOR).$(PLUS_VER_PATCH) -> $(PLUS_PTF)"
	@echo "Mini     (IMX) : $(EDITION_NAME) $(MINI_VER_MAJOR).$(MINI_VER_MINOR).$(MINI_VER_PATCH) -> $(MINI_PTF)"

TARGET ?= all

bump-patch:
	@python3 -c 'import re, sys; t="$(TARGET)".upper(); p="version.mk"; s=open(p).read(); \
	targets = ["ORIGINAL", "PLUS", "MINI"] if t == "ALL" else [t]; \
	for k in targets: \
	    m = re.search(rf"{k}_VER_PATCH\s*:=\s*(\d+)", s); \
	    assert m, f"Unknown TARGET {t}"; \
	    v = int(m.group(1)) + 1; \
	    s = re.sub(rf"{k}_VER_PATCH\s*:=\s*\d+", f"{k}_VER_PATCH := {v}", s); \
	open(p, "w").write(s); print(f"Bumped patch version for {targets}")'

bump-minor:
	@python3 -c 'import re, sys; t="$(TARGET)".upper(); p="version.mk"; s=open(p).read(); \
	targets = ["ORIGINAL", "PLUS", "MINI"] if t == "ALL" else [t]; \
	for k in targets: \
	    m = re.search(rf"{k}_VER_MINOR\s*:=\s*(\d+)", s); \
	    assert m, f"Unknown TARGET {t}"; \
	    v = int(m.group(1)) + 1; \
	    s = re.sub(rf"{k}_VER_MINOR\s*:=\s*\d+", f"{k}_VER_MINOR := {v}", s); \
	    s = re.sub(rf"{k}_VER_PATCH\s*:=\s*\d+", f"{k}_VER_PATCH := 0", s); \
	open(p, "w").write(s); print(f"Bumped minor version for {targets} (patch reset to 0)")'
