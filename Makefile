# File: Makefile
# Purpose: Provide root-level convenience targets for cleaning CMake builds.
# Links: scripts/clean.sh scripts/clean.ps1

.PHONY: help clean distclean nuke
BUILD ?= build
help:
	@echo "Targets:"
	@echo "  clean     - cmake --build $(BUILD) --target clean [use CONFIG=Debug|Release]"
	@echo "  distclean - scrub CMake files inside $(BUILD)"
	@echo "  nuke      - delete build* dirs via scripts/clean.sh or scripts/clean.ps1"
clean:
	# Single-config:
	@if [ -d "$(BUILD)" ]; then cmake --build $(BUILD) --target clean; else echo "No $(BUILD)/ dir"; fi
	# Multi-config (use CONFIG var):
	@if [ -n "$$CONFIG" ]; then cmake --build $(BUILD) --config $$CONFIG --target clean; fi
distclean:
	@if [ -d "$(BUILD)" ]; then (cd $(BUILD) && cmake --build . --target distclean || cmake -P ../cmake/DistClean.cmake); else echo "No $(BUILD)/ dir"; fi
nuke:
	@./scripts/clean.sh
