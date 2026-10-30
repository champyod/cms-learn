# Workspace entry points.
#
# This builds and checks the C++ and Rust artifacts. Running the stack stays with
# `deploy/docker-compose.yml`, because a service that is deployed by two different commands is a
# service with two configurations; `make upgrade` is an entry point to `tools/upgrade.sh` rather than
# a second implementation of the deploy sequence.

BUILD_DIR ?= build
BUILD_TYPE ?= RelWithDebInfo
CMAKE ?= cmake
CTEST ?= ctest
CARGO ?= cargo

.DEFAULT_GOAL := help

.PHONY: help configure build test gate upgrade clean

help:
	@printf '%s\n' \
	  'configure   generate the build tree' \
	  'build       compile the workspace' \
	  'test        run the C++ tests' \
	  'gate        run every repository gate' \
	  'upgrade     deploy the release release.toml names, with a rollback on failure' \
	  'clean       remove build outputs'

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel

test: build
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

gate: test
	bun run gate:all
	bash tools/check-scaffold.sh

upgrade:
	bash tools/upgrade.sh

clean:
	$(CMAKE) -E rm -rf $(BUILD_DIR)
