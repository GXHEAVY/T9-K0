# T9-K0 build entry points. `make help` lists targets.
# SPDX-License-Identifier: GPL-2.0-or-later

QMK_REPO := https://github.com/Keychron/qmk_firmware.git
QMK_COMMIT := c5d998442710b38650903937249144a0cd728ebe
QMK_CLI_VERSION := 1.2.0
QMK_DIR := qmk_firmware
QMK_SUBMODULES := lib/chibios lib/chibios-contrib lib/printf lib/lufa lib/vusb
VENV := .venv
KEYBOARD := keychron/k0_max
KEYMAP := t9
KEYMAP_LINK := $(QMK_DIR)/keyboards/$(KEYBOARD)/keymaps/$(KEYMAP)
REQUIRED_TOOLS := git python3 arm-none-eabi-gcc dfu-util dfu-suffix
TARGET := $(subst /,_,$(KEYBOARD))_$(KEYMAP)
EXPERIMENT_TARGET := $(TARGET)_experiment
STOCK_TARGET := $(subst /,_,$(KEYBOARD))_keychron
ELF ?= $(QMK_DIR)/.build/$(TARGET).elf
FLASH_BYTES := 262144
EEPROM_BACKING_BYTES := 4096

HOST_CC ?= cc
HOST_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -Ikeymap -Ihost
HOST_BUILD := host/build
ENGINE_SRC := keymap/t9_engine.c keymap/t9_output.c keymap/t9_contractions.c keymap/t9_dict_simple.c
HOST_HEADERS := $(filter-out keymap/t9_dictionary_data.h,$(wildcard keymap/*.h)) $(wildcard host/qmk_stub/*.h) host/t9_test.h Makefile
PACKED_SRC := keymap/t9_dict_packed.c

DICT_VENV := .venv-dict
DEV_VENV := .venv-dev
PYTHON ?= python3
DICT_WORDS := dictionary/words_en.tsv
DICT_HEADER := keymap/t9_dictionary_data.h
DICT_MAX_BYTES ?= 150000

export PATH := $(CURDIR)/$(VENV)/bin:$(PATH)

.PHONY: help setup check-tools build build-experiment build-stock flash flash-experiment flash-stock clean test sim dict fetch-sources sizes fixtures keymap-doc check-python setup-dictionary setup-dev format check-format check-generated test-tools

help:
	@echo "setup            venv + pinned Keychron QMK checkout + keymap symlink"
	@echo "build            firmware with the production dictionary, prints flash budget"
	@echo "build-experiment diagnostic firmware with a small reference dictionary"
	@echo "build-stock      stock keychron keymap from the same checkout (recovery image)"
	@echo "test             host unit, event-sequence, scenario and dictionary tests"
	@echo "sim              build the terminal simulator ($(HOST_BUILD)/t9_simulator)"
	@echo "dict             regenerate $(DICT_HEADER) from $(DICT_WORDS)"
	@echo "fetch-sources    download pinned word sources and rebuild $(DICT_WORDS)"
	@echo "sizes            flash/RAM report for the last firmware build"
	@echo "flash            flash via DFU (hardware required); also flash-experiment, flash-stock"
	@echo "keymap-doc       regenerate the key grids in KEYMAP.md from keymap_grid.py"
	@echo "setup-dev        install pinned formatting tools"
	@echo "format           format hand-written C and Python"
	@echo "check-format     check formatting and Python lint"
	@echo "check-generated  verify the dictionary and keymap diagrams"
	@echo "test-tools       test setup recovery and dictionary input validation"

setup: check-tools
	$(PYTHON) scripts/setup.py venv "$(VENV)" --package qmk==$(QMK_CLI_VERSION)
	$(PYTHON) scripts/setup.py qmk "$(QMK_DIR)" --repo "$(QMK_REPO)" --commit "$(QMK_COMMIT)" \
		--link "$(KEYMAP_LINK)" --source keymap --submodules $(QMK_SUBMODULES)

check-python:
	@$(PYTHON) -c 'import sys; sys.exit("Python 3.12 or newer is required") if sys.version_info < (3, 12) else None'

check-tools: check-python
	@missing="$(strip $(foreach tool,$(REQUIRED_TOOLS),$(if $(shell command -v $(tool)),,$(tool))))"; \
	if [ -n "$$missing" ]; then \
		echo "missing tools: $$missing"; \
		echo "  arm-none-eabi-gcc: brew install --cask gcc-arm-embedded"; \
		echo "  dfu-util, dfu-suffix: brew install dfu-util"; \
		exit 1; \
	fi

build: setup
	$(MAKE) -C $(QMK_DIR) $(KEYBOARD):$(KEYMAP) SKIP_GIT=yes
	@$(MAKE) --no-print-directory sizes

build-experiment: setup
	$(MAKE) -C $(QMK_DIR) $(KEYBOARD):$(KEYMAP) SKIP_GIT=yes T9_DICT=experiment TARGET=$(EXPERIMENT_TARGET)
	@$(MAKE) --no-print-directory sizes ELF=$(QMK_DIR)/.build/$(EXPERIMENT_TARGET).elf

build-stock: setup
	$(MAKE) -C $(QMK_DIR) $(KEYBOARD):keychron SKIP_GIT=yes
	@$(MAKE) --no-print-directory sizes ELF=$(QMK_DIR)/.build/$(STOCK_TARGET).elf

sizes:
	@arm-none-eabi-size -A $(ELF) | awk ' \
		$$1 == ".vectors" || $$1 == ".text" || $$1 == ".rodata" || $$1 == ".ARM.exidx" || $$1 == ".data" { flash += $$2 } \
		$$1 == ".data" || $$1 == ".bss" { ram += $$2 } \
		$$1 == ".mstack" || $$1 == ".pstack" { stack += $$2 } \
		$$1 == ".heap" { heap = $$2 } \
		END { printf "flash used: %d B, free: %d B (of %d minus %d EEPROM backing)\n", flash, $(FLASH_BYTES) - $(EEPROM_BACKING_BYTES) - flash, $(FLASH_BYTES), $(EEPROM_BACKING_BYTES); \
		      printf "ram static: %d B (.data + .bss), stacks %d B, heap remainder %d B of 65536\n", ram, stack, heap }'
	@arm-none-eabi-nm --size-sort -S -t d $(ELF) | grep -i " t9_\| engine$$\| bindings$$" | awk '{ printf "  %7d %s %s\n", $$2, $$3, $$4 }' | sort -rn | head -24

flash: setup
	$(MAKE) -C $(QMK_DIR) $(KEYBOARD):$(KEYMAP):flash SKIP_GIT=yes

flash-experiment: setup
	$(MAKE) -C $(QMK_DIR) $(KEYBOARD):$(KEYMAP):flash SKIP_GIT=yes T9_DICT=experiment TARGET=$(EXPERIMENT_TARGET)

flash-stock: setup
	$(MAKE) -C $(QMK_DIR) $(KEYBOARD):keychron:flash SKIP_GIT=yes

clean:
	rm -rf $(QMK_DIR)/.build $(QMK_DIR)/*.bin $(HOST_BUILD)

# --- host-side tests and simulator -------------------------------------------

$(HOST_BUILD):
	mkdir -p $(HOST_BUILD)

host/t9_fixture_table.h: host/t9_fixture_words.txt
	@awk -F'\t' 'BEGIN { print "/* generated from host/t9_fixture_words.txt by make; do not edit */" } \
		/^[a-z]/ { s = int($$2 * 8 + 0.5); if (s > 63) s = 63; printf "{\"%s\", %d},\n", $$1, s }' $< > $@

host/t9_fixture_packed.h: host/t9_fixture_words.txt dictionary/build_t9_dictionary.py
	$(PYTHON) dictionary/build_t9_dictionary.py --input $< --packed $@ --block-size 8 --quiet

fixtures: host/t9_fixture_table.h host/t9_fixture_packed.h

$(HOST_BUILD)/t9_engine_test: host/t9_engine_test.c host/t9_test.h $(ENGINE_SRC) $(HOST_HEADERS) host/t9_fixture_table.h | $(HOST_BUILD)
	$(HOST_CC) $(HOST_CFLAGS) host/t9_engine_test.c $(ENGINE_SRC) -o $@

$(HOST_BUILD)/t9_scenario_test: host/t9_scenario_test.c host/t9_test.h $(ENGINE_SRC) $(HOST_HEADERS) host/t9_fixture_table.h | $(HOST_BUILD)
	$(HOST_CC) $(HOST_CFLAGS) host/t9_scenario_test.c $(ENGINE_SRC) -o $@

$(HOST_BUILD)/t9_dict_test: host/t9_dict_test.c $(HOST_HEADERS) $(PACKED_SRC) keymap/t9_dict_simple.c host/t9_fixture_table.h host/t9_fixture_packed.h | $(HOST_BUILD)
	$(HOST_CC) $(HOST_CFLAGS) -DT9_DICT_DATA_HEADER='"t9_fixture_packed.h"' host/t9_dict_test.c $(PACKED_SRC) keymap/t9_dict_simple.c -o $@

$(HOST_BUILD)/t9_dict_production_test: host/t9_dict_production_test.c host/t9_test.h $(PACKED_SRC) $(DICT_HEADER) $(ENGINE_SRC) $(HOST_HEADERS) | $(HOST_BUILD)
	$(HOST_CC) $(HOST_CFLAGS) host/t9_dict_production_test.c $(PACKED_SRC) $(ENGINE_SRC) -o $@

$(HOST_BUILD)/t9_keymap_pc_test: HOST_CFLAGS += -DT9_TEST_PC

$(HOST_BUILD)/t9_keymap_test $(HOST_BUILD)/t9_keymap_pc_test: host/t9_keymap_test.c $(wildcard host/qmk_stub/*.h) host/t9_test.h keymap/keymap.c keymap/config.h keymap/t9_experiment_words.h keymap/t9_shortcuts.c keymap/t9_shortcuts.h $(ENGINE_SRC) $(HOST_HEADERS) | $(HOST_BUILD)
	$(HOST_CC) $(HOST_CFLAGS) -Ihost/qmk_stub host/t9_keymap_test.c keymap/t9_shortcuts.c $(ENGINE_SRC) -o $@

test: check-python $(HOST_BUILD)/t9_engine_test $(HOST_BUILD)/t9_keymap_test $(HOST_BUILD)/t9_keymap_pc_test $(HOST_BUILD)/t9_scenario_test $(HOST_BUILD)/t9_dict_test $(HOST_BUILD)/t9_dict_production_test $(HOST_BUILD)/t9_simulator
	$(HOST_BUILD)/t9_engine_test
	$(HOST_BUILD)/t9_keymap_test
	$(HOST_BUILD)/t9_keymap_pc_test
	$(HOST_BUILD)/t9_scenario_test
	$(HOST_BUILD)/t9_dict_test
	$(HOST_BUILD)/t9_dict_production_test
	$(HOST_BUILD)/t9_simulator --script host/scenario_contractions.t9 --pace 50
	$(HOST_BUILD)/t9_simulator --script host/scenario_navigation.t9
	$(HOST_BUILD)/t9_simulator --script host/scenario_navigation.t9 --pace 50
	$(HOST_BUILD)/t9_simulator --script host/scenario_wrappers.t9
	$(HOST_BUILD)/t9_simulator --script host/scenario_wrappers.t9 --pace 50
	$(HOST_BUILD)/t9_simulator --script host/scenario_completion_family.t9
	$(HOST_BUILD)/t9_simulator --script host/scenario_completion_family.t9 --pace 50

$(HOST_BUILD)/t9_simulator: host/t9_simulator.c $(ENGINE_SRC) $(HOST_HEADERS) $(PACKED_SRC) $(DICT_HEADER) host/t9_fixture_table.h | $(HOST_BUILD)
	$(HOST_CC) $(HOST_CFLAGS) host/t9_simulator.c $(ENGINE_SRC) $(PACKED_SRC) -o $@

sim: check-python $(HOST_BUILD)/t9_simulator
	@echo "run: $(HOST_BUILD)/t9_simulator        (interactive)"
	@echo "     $(HOST_BUILD)/t9_simulator --dict fixture --script host/scenario_paragraph.t9"

# --- dictionary ----------------------------------------------------------------

setup-dictionary: check-python
	$(PYTHON) scripts/setup.py venv "$(DICT_VENV)" --requirements dictionary/requirements.txt

fetch-sources: setup-dictionary
	$(DICT_VENV)/bin/python dictionary/fetch_sources.py --output $(DICT_WORDS)

dict: $(DICT_WORDS) dictionary/build_t9_dictionary.py
	$(PYTHON) dictionary/build_t9_dictionary.py --input $(DICT_WORDS) --packed $(DICT_HEADER) \
		--max-bytes $(DICT_MAX_BYTES) --check-words host/t9_fixture_words.txt --report dictionary/report.md

keymap-doc:
	$(PYTHON) keymap_grid.py KEYMAP.md

# --- contributor checks --------------------------------------------------------

setup-dev: check-python
	$(PYTHON) scripts/setup.py venv "$(DEV_VENV)" --requirements scripts/requirements-dev.txt

format: setup-dev
	$(PYTHON) scripts/format.py --tools "$(DEV_VENV)/bin"

check-format: setup-dev
	$(PYTHON) scripts/format.py --tools "$(DEV_VENV)/bin" --check

check-generated: check-python
	$(PYTHON) scripts/check_generated.py

test-tools: check-python
	$(PYTHON) -m unittest discover -s tests -v
