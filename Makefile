# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

CMAKE:=cmake
CMAKE_ARGS:=
CCACHE:=ccache
NINJA:=$(shell command -v ninja 2> /dev/null)
CLANG_FORMAT:=$(shell command -v clang-format 2> /dev/null)
PYTHON:=python3

PROJECT_DIR=$(realpath $(dir $(realpath $(lastword $(MAKEFILE_LIST)))))

# Build options
BUILD_TOOL=$(MAKE)
ifdef NPROC
JOBS=$(NPROC)
endif

ifdef JOBS
BUILD_ARGS=-j $(JOBS)
else
BUILD_ARGS=-j
endif

BUILD_DIRECTORY_PREFIX=cmake-build-
DEFAULT_BUILD_TYPE=release
BUILD_TYPE:=$(DEFAULT_BUILD_TYPE)
BUILD_DIRECTORY?=$(BUILD_DIRECTORY_PREFIX)$(BUILD_TYPE)

# Git
GIT_DEFAULT_BRANCH=main
GIT_DIVERGENCE_REF:=$(shell git rev-parse --short $(shell git merge-base HEAD origin/$(GIT_DEFAULT_BRANCH)))
GIT_BUILD_REF=$(BUILD_DIRECTORY)/git_divergence_ref.txt

# Skip configuration if CMake already configured
# (will use all cached properties since it was last configured)
ifndef CI
SKIP_CONFIGURE?=0
# Default to "fresh" if git divergence ref has changed since last build
ifeq ($(shell test -e $(GIT_BUILD_REF) && cat $(GIT_BUILD_REF)),$(GIT_DIVERGENCE_REF))
CMAKE_ALWAYS_FRESH?=0
else
CMAKE_ALWAYS_FRESH?=1
endif
else
SKIP_CONFIGURE?=1
CMAKE_ALWAYS_FRESH?=0
endif

# Fresh (i.e. clear CMake cache)
CMAKE_FRESH_ARGS=
ifeq ($(CMAKE_ALWAYS_FRESH),1)
CMAKE_FRESH_ARGS=--fresh
endif

# Install options
CMAKE_INSTALL_ARGS=
ifdef INSTALL_DIRECTORY
CMAKE_INSTALL_ARGS=--install-prefix $(if $(patsubst /%,,$(INSTALL_DIRECTORY)),$(PROJECT_DIR)/)$(INSTALL_DIRECTORY)
endif

# Configure (cmake)
CMAKE_CONFIGURE_ARGS=$(CMAKE_FRESH_ARGS) $(CMAKE_INSTALL_ARGS) -D CMAKE_BUILD_TYPE=$(BUILD_TYPE) $(CMAKE_ARGS)

# Testing options
CTEST_BIN:=ctest
CTEST_TIMEOUT?=1500
CTEST_CI_OPTIONS:=--output-junit report.xml --output-log report.log
CTEST_DEFAULT_OPTIONS=--output-on-failure --timeout $(CTEST_TIMEOUT)
ifdef CI
CTEST_OPTIONS:=$(CTEST_DEFAULT_OPTIONS) $(CTEST_CI_OPTIONS)
else
CTEST_OPTIONS:=$(CTEST_DEFAULT_OPTIONS)
endif
CTEST_TYPE_OPTIONS:=
CTEST=$(CTEST_BIN) $(CTEST_OPTIONS)

CTEST_UNIT_TEST_EXCLUDE=integration|regression|benchmark|veryslow
CTEST_TEST_EXCLUDE=veryslow

# Enables memory-leak checks in ASAN/Valgrind
LEAK_CHECKS:=1

# Valgrind options
VALGRIND:=$(shell command -v valgrind 2> /dev/null) # Valgrind needs fullpath
VALGRIND_BUILD_TYPE=debug
VALGRIND_SUPP=$(PROJECT_DIR)/.valgrind.supp
VALGRIND_LEAK_SUPP=$(PROJECT_DIR)/.valgrind.leak.supp
VALGRIND_BASE_OPTIONS=--track-origins=yes --suppressions=$(VALGRIND_SUPP)
ifeq ($(LEAK_CHECKS),1)
VALGRIND_OPTIONS:=$(VALGRIND_BASE_OPTIONS) --leak-check=full --suppressions=$(VALGRIND_LEAK_SUPP)
else
VALGRIND_OPTIONS:=$(VALGRIND_BASE_OPTIONS)
endif
CTEST_MEMCHECK_OPTIONS=-T memcheck --overwrite MemoryCheckCommand=$(VALGRIND) --overwrite MemoryCheckCommandOptions='$(VALGRIND_OPTIONS)'
CTEST_MEMCHECK_UNIT_FILTER=-E 'py|python|evp|tracing_integration|sim_sys|dryrun' -LE 'python|integration|regression|benchmark'

# ASAN options
ASAN_BUILD_TYPE=asan
ASAN_SYMBOLIZER:=$(shell command -v llvm-symbolizer 2> /dev/null)
ASAN_SUPP=$(PROJECT_DIR)/.asan.supp
LSAN_SUPP=$(PROJECT_DIR)/.lsan.supp
ifeq ($(LEAK_CHECKS),1)
ASAN_ENV=ASAN_OPTIONS='detect_leaks=1:suppressions=$(ASAN_SUPP)'
LSAN_ENV=LSAN_OPTIONS='suppressions=$(LSAN_SUPP)'
else
ASAN_ENV=ASAN_OPTIONS='detect_leaks=0:suppressions=$(ASAN_SUPP)'
endif

ifdef ASAN_SYMBOLIZER
ASAN_ENV=$(ASAN_ENV) ASAN_SYMBOLIZER_PATH=$(ASAN_SYMBOLIZER)
endif

CTEST_ASAN_EXCLUDE=py|python|evp|event_stream_unit|tracing_integration|sim_sys
CTEST_ASAN_LABEL_EXCLUDE=python|regression|benchmark
CTEST_ASAN_FILTER=-E '$(CTEST_ASAN_EXCLUDE)' -LE '$(CTEST_ASAN_LABEL_EXCLUDE)|$(CTEST_TEST_EXCLUDE)'
CTEST_ASAN_UNIT_FILTER=-E '$(CTEST_ASAN_EXCLUDE)' -LE '$(CTEST_ASAN_LABEL_EXCLUDE)|$(CTEST_UNIT_TEST_EXCLUDE)'

# Coverage options
COVERAGE_BUILD_TYPE:=coverage
COVERAGE_RESULTS:=coverage
COVERAGE_GENHTML_ARGS:=
COVERAGE_JOBS:=8
ifdef JOBS
COVERAGE_JOBS=$(JOBS)
endif

# Drift/snapshot options
SNAPSHOT_SUFFIX=.snapshot
DRIFT_SUFFIX=.drift
LATEST_DRIFT=latest$(DRIFT_SUFFIX)
REGRESS_DIR=$(PROJECT_DIR)/src/ampere/regress
SNAPSHOT_DIR:=$(PROJECT_DIR)
DRIFT_STUDY=results/results.by_trace.csv
DRIFT_ERRORS=results.errors
DRIFT_SPEC_TXT=specification.txt
DRIFT_TLIST_TXT=tracelist.txt
DRIFT_SPEC:=latest
DRIFT_TLIST?=

# Linting options
PROJECT_CONFIG_DIR=$(PROJECT_DIR)/src/ampere/config
PROJECT_PYTHON_DIR=$(PROJECT_DIR)/src/ampere/python/panthera
LINT_JOBS:=8
ifdef JOBS
LINT_JOBS=$(JOBS)
endif
# Use the shared file enumerator so vendored sources remain outside Panthera's
# repository-wide formatting and linting checks.
FIND_AMPERE_SRCS=$(PROJECT_DIR)/infrastructure/checks/files.sh
FIND_CHANGED_AMPERE_SRCS=git diff $(GIT_DEFAULT_BRANCH) --name-only --
ifdef CHANGED_LAST_ONLY
	FIND_CHANGED_AMPERE_SRCS=git show --name-only --pretty=format: HEAD --
endif
CPP_FILES='*.c' '*.cc' '*.h' '*.hpp'
ASM_FILES='*.s' '*.S'
PY_FILES='*.py'
PY_LINT_FILES=$(PY_FILES) ':!:*/ampere/decoders/*'
MD_FILES='*.md'
CFG_FILES='*.cfg'
SHELL_FILES='*.sh'
TXT_FILES='*.txt'
JSON_FILES='*.json'
YAML_FILES='*.yaml' '*.yml'
NON_SOURCE_TEXT_FILES=$(TXT_FILES) $(MD_FILES) $(CFG_FILES)
PYTHON_LINT_FILTER=-x '*/ampere/decoders/*'
FILTER_UNIQ_FILES=grep -v ' ' | xargs realpath | uniq
RUFF_ARGS:=

# Policies
TEXT_FILES_REQUIRING_NEWLINES=$(CPP_FILES) \
							  $(ASM_FILES) \
							  $(PY_FILES) \
							  $(MD_FILES) \
							  $(CFG_FILES) \
							  $(SHELL_FILES) \
							  $(TXT_FILES) \
							  $(JSON_FILES) \
							  $(YAML_FILES) \
							  '*.nix' '*.log'
TEXT_FILES_REQUIRING_SPACES=$(CPP_FILES) \
							$(ASM_FILES) \
							$(PY_FILES) \
							$(TXT_FILES) \
							$(MD_FILES) \
							$(CFG_FILES)

# Misc building options
ifndef VERBOSE
MAKEFLAGS += --no-print-directory
endif

ifndef CI
MAKEFLAGS += --quiet
endif

ifdef NINJA
BUILD_TOOL=$(NINJA)
ifndef JOBS
BUILD_ARGS=
endif
export CMAKE_GENERATOR=Ninja
endif

ifdef CTEST_NPROC
export CTEST_PARALLEL_LEVEL=$(CTEST_NPROC)
else ifdef JOBS
export CTEST_PARALLEL_LEVEL=$(JOBS)
else
export CTEST_PARALLEL_LEVEL=8
endif

# Ccache options
export CCACHE_BASEDIR=$(PROJECT_DIR)
ifndef CCACHE_LOCAL_ONLY
export CCACHE_COMPILERCHECK=%compiler% -v
export CCACHE_SLOPPINESS=locale,pch_defines,time_macros
export CCACHE_DEPEND=true
export CCACHE_NOHASHDIR=true
endif

export GTEST_BRIEF=1
ifndef CI
export GTEST_COLOR=1
endif

default: install


################################################################################
# Configuration
################################################################################

.PHONY: .optional-configure
.optional-configure:
	mkdir -p $(BUILD_DIRECTORY)
ifeq ($(SKIP_CONFIGURE),0)
	cd $(BUILD_DIRECTORY) && $(CMAKE) $(CMAKE_CONFIGURE_ARGS) -S ..
else
	cd $(BUILD_DIRECTORY) && ( [ -f CMakeCache.txt ] || $(CMAKE) $(CMAKE_CONFIGURE_ARGS) -S .. )
endif
	echo -n $(GIT_DIVERGENCE_REF) > $(GIT_BUILD_REF)

.PHONY: optional-configure-%
optional-configure-%:
	@$(MAKE) .optional-configure BUILD_TYPE=$*

.PHONY: configure-%
configure-%:
	@# Configure (CMake) specified build type
	@$(MAKE) optional-configure-$* SKIP_CONFIGURE=0

.PHONY: configure
configure: configure-$(DEFAULT_BUILD_TYPE)
	@# Configure (CMake) default build type

.PHONY: .clang-only
.clang-only:
	@( [ "$$CC" == "clang" ] || ( echo "clang required!" && exit 1 ) )


################################################################################
# Building
################################################################################

.PHONY: .build
.build:
	$(BUILD_TOOL) -C $(BUILD_DIRECTORY) $(BUILD_ARGS) $(TARGET)

.PHONY: build-%
build-%:
	@# Build specified type
	@$(MAKE) .optional-configure BUILD_TYPE=$*
	@$(MAKE) .build BUILD_TYPE=$*

.PHONY: build
build: build-$(DEFAULT_BUILD_TYPE)
	@# Build default

.PHONY: install-%
install-%:
	@# Build and install specified build type
	@$(MAKE) build-$* TARGET=install

.PHONY: install
install: install-$(DEFAULT_BUILD_TYPE)
	@# Build and install

.PHONY: debug
debug: install-debug
	@# Build for debug

.PHONY: debug-messages
debug-messages: install-releasewithdebmessages
	@# Build release with debug messages


################################################################################
# Tests
################################################################################

# Tests can be filtered/specified using 'TEST=name' or 'TESTS=regex'

.PHONY: list-tests
list-tests:
	@# List all tests (known to CTest)
	cd $(BUILD_DIRECTORY) && $(CTEST) --show-only

.PHONY: .test
.test:
ifdef TEST
	cd $(BUILD_DIRECTORY) && $(CTEST) $(CTEST_TYPE_OPTIONS) -R '^$(TEST)$$' $(CTEST_ARGS)
else ifdef TESTS
	cd $(BUILD_DIRECTORY) && $(CTEST) $(CTEST_TYPE_OPTIONS) -R '$(TESTS)' $(CTEST_ARGS)
else
	cd $(BUILD_DIRECTORY) && $(CTEST) $(CTEST_TYPE_OPTIONS) $(CTEST_ARGS)
endif

.PHONY: test-only-%
test-only-%:
	@$(MAKE) .test BUILD_TYPE=$*

.PHONY: test-only
test-only: test-only-$(DEFAULT_BUILD_TYPE)

.PHONY: test-%
test-%:
	@# Run tests for specified build
ifdef TEST
	@$(MAKE) build-$* TARGET=$(TEST)
else
	@$(MAKE) install-$*
endif
	@$(MAKE) test-only-$*

.PHONY: test
test: test-$(DEFAULT_BUILD_TYPE)
	@# Run tests

.PHONY: unit-test-%
unit-test-%:
	@# Run unit tests for specified build
	@$(MAKE) test-$* CTEST_ARGS="$(CTEST_ARGS) -LE '$(CTEST_UNIT_TEST_EXCLUDE)'"

.PHONY: unit-test
unit-test: unit-test-$(DEFAULT_BUILD_TYPE)
	@# Run unit tests only

.PHONY: integration-%
integration-%:
	@# Run integration tests for specified build
	@$(MAKE) test-$* CTEST_ARGS="$(CTEST_ARGS) -L 'integration|regression' -LE '$(CTEST_TEST_EXCLUDE)'"

.PHONY: integration
integration: integration-$(DEFAULT_BUILD_TYPE)
	@# Run integration tests only

.PHONY: all-tests-%
all-tests-%:
	@# Run all tests for specified build
	@$(MAKE) test-$*

.PHONY: all-tests
all-tests: all-tests-$(DEFAULT_BUILD_TYPE)
	@# Run all tests

.PHONY: benchmark-%
benchmark-%:
	@# Run all benchmarks for specified build
	@$(MAKE) test-$* CTEST_ARGS="$(CTEST_ARGS) -L benchmark"

.PHONY: benchmark
benchmark: benchmark-$(DEFAULT_BUILD_TYPE)
	@# Run all benchmarks


################################################################################
# Coverage
################################################################################

.PHONY: coverage
coverage:
	@# Build for coverage collection
	@$(MAKE) build-$(COVERAGE_BUILD_TYPE)

.PHONY: coverage-report
coverage-report:
	@# C++/Python HTML coverage report
	mkdir -p $(COVERAGE_RESULTS)
	genhtml \
		coverage.info python_coverage.info $(COVERAGE_GENHTML_ARGS) \
		--keep-going --ignore-errors unused,inconsistent,source \
		--output-directory $(COVERAGE_RESULTS)/lcov

.PHONY: coverage-xml
coverage-xml:
	@# C++/Python coverage in XML
	mkdir -p $(COVERAGE_RESULTS)
	lcov_cobertura coverage.info --output $(COVERAGE_RESULTS)/cobertura-cpp.xml
	$(PYTHON) -m coverage xml -o $(COVERAGE_RESULTS)/cobertura-python-tests.xml

.PHONY: coverage-cpp-report
coverage-cpp-report:
	@# Show C++ per-file coverage
	lcov -l coverage.info

.PHONY: .coverage-lcov
.coverage-lcov:
	lcov -j $(COVERAGE_JOBS) -c \
		-d "$(BUILD_DIRECTORY)" \
		--build-directory "$(BUILD_DIRECTORY)" \
		--source-directory "$(PROJECT_DIR)" \
		--rc max_message_count=0 \
		--ignore-errors unused,inconsistent,source -o coverage.raw.info 2>lcov.log \
		|| ( cat lcov.log && exit 1 )
	lcov -o coverage.info -r coverage.raw.info --ignore-errors unused \
		"*/$(BUILD_DIRECTORY)/src/*" \
		"*/src/ampere/adis*" \
		"*/src/effolkronium*" \
		"*/src/elfio*" \
		"*/src/**/test/*" \
		"*/src/**/test_*.cc" \
		"*/include/boost/*" \
		"/nix/store/*"
	sed -i 's|SF:$(PROJECT_DIR)/|SF:|g' coverage.info

.PHONY: coverage-lcov
coverage-lcov:
	@# Collect C++ coverage
	$(MAKE) .coverage-lcov BUILD_TYPE=$(COVERAGE_BUILD_TYPE)

.PHONY: coverage-python
coverage-python:
	@# Collect python coverage
	$(PYTHON) -m coverage combine --keep $$(echo $$(find $(BUILD_DIRECTORY) -name '.coverage*' -type f))
	$(PYTHON) -m coverage lcov -o python_coverage.info

.PHONY: coverage-python-report
coverage-python-report:
	@# Python coverage report
	mkdir -p $(COVERAGE_RESULTS)
	$(PYTHON) -m coverage report

.PHONY: .coverage-reset
.coverage-reset:
	lcov -z --directory $(BUILD_DIRECTORY)
	rm -f *.info
	find $(BUILD_DIRECTORY) -name '.coverage*' | xargs rm
	$(PYTHON) -m coverage erase

.PHONY: coverage-reset
coverage-reset:
	@# Reset coverage
	@$(MAKE) .coverage-reset BUILD_TYPE=$(COVERAGE_BUILD_TYPE)


################################################################################
# Memory Checkers
################################################################################

.PHONY: clean-memcheck-testing-logs
clean-testing-logs:
	rm -f $(BUILD_DIRECTORY)/Testing/Temporary/*.log

.PHONY: clean-memcheck-testing-logs-%
clean-testing-logs-%:
	@$(MAKE) clean-testing-logs BUILD_TYPE=$*

.PHONY: show-testing-logs
show-testing-logs:
	grep -l -E 'ERROR SUMMARY: [1-9]+[0-9]* errors' $(BUILD_DIRECTORY)/Testing/Temporary/MemoryChecker.*.log | xargs cat

.PHONY: show-testing-logs-%
show-testing-logs-%:
	@$(MAKE) show-testing-logs BUILD_TYPE=$*

# ASAN (address sanitizer)

.PHONY: asan
asan: install-$(ASAN_BUILD_TYPE)
	@# Build for ASAN (address sanitizer)

.PHONY: asan-unit-test-only
asan-unit-test-only: clean-testing-logs-$(ASAN_BUILD_TYPE)
	@$(ASAN_ENV) $(LSAN_ENV) $(MAKE) .test BUILD_TYPE=$(ASAN_BUILD_TYPE) CTEST_ARGS="$(CTEST_ASAN_UNIT_FILTER)"

.PHONY: asan-unit-test
asan-unit-test: asan
	@# Run unit-tests under ASAN
	@$(MAKE) asan-unit-test-only || $(MAKE) show-testing-logs-$(ASAN_BUILD_TYPE)

.PHONY: asan-test-only
asan-test-only: clean-testing-logs-$(ASAN_BUILD_TYPE)
	@$(ASAN_ENV) $(LSAN_ENV) $(MAKE) .test BUILD_TYPE=$(ASAN_BUILD_TYPE) CTEST_ARGS="$(CTEST_ASAN_FILTER)"

.PHONY: asan-test
asan-test: asan
	@# Run tests under ASAN
	@$(MAKE) asan-test-only || $(MAKE) show-testing-logs-$(ASAN_BUILD_TYPE)

# Valgrind

.PHONY: valgrind
valgrind: build-$(VALGRIND_BUILD_TYPE)
	@# Build for valgrind

.PHONY: valgrind-unit-test-only
valgrind-unit-test-only: clean-testing-logs-$(VALGRIND_BUILD_TYPE)
	@$(MAKE) .test BUILD_TYPE=$(VALGRIND_BUILD_TYPE) CTEST_ARGS="$(CTEST_MEMCHECK_OPTIONS) $(CTEST_MEMCHECK_UNIT_FILTER)"

.PHONY: valgrind-unit-test
valgrind-unit-test: valgrind
	@# Run unit-tests under valgrind
	@$(MAKE) valgrind-unit-test-only || $(MAKE) show-testing-logs-$(VALGRIND_BUILD_TYPE)


################################################################################
# Drift/Characterization
################################################################################

.PHONY: .verify-drift-settings
.verify-drift-settings:
	@[ "$$(diff $(LATEST_DRIFT)/$(DRIFT_SPEC_TXT) $(GIT_DIVERGENCE_REF)$(DRIFT_SUFFIX)/$(DRIFT_SPEC_TXT))" == "" ] || ( echo "Different DRIFT_SPEC used (remove old results)" && exit 1 )
	@[ "$$(diff $(LATEST_DRIFT)/$(DRIFT_TLIST_TXT) $(GIT_DIVERGENCE_REF)$(DRIFT_SUFFIX)/$(DRIFT_TLIST_TXT))" == "" ] || ( echo "Different DRIFT_TLIST used (remove old results)" && exit 1 )

.PHONY: .require-drift-tracelist
.require-drift-tracelist:
	@[ -n "$(DRIFT_TLIST)" ] || ( echo "Set DRIFT_TLIST to a workload manifest before running drift targets" && exit 1 )
	@[ -f "$(DRIFT_TLIST)" ] || ( echo "No drift tracelist: $(DRIFT_TLIST)" && exit 1 )

%$(SNAPSHOT_SUFFIX):
	$(REGRESS_DIR)/build_ref.sh . $*$(SNAPSHOT_SUFFIX) $*

%$(DRIFT_SUFFIX):
	@$(MAKE) install &> /dev/null
	@echo "Running $*..."
	mkdir -p $*$(DRIFT_SUFFIX)
	DRIFT_TLIST="$(DRIFT_TLIST)" $(REGRESS_DIR)/drift.sh $*$(DRIFT_SUFFIX) $(SNAPSHOT_DIR) $(PROJECT_DIR) $(DRIFT_SPEC)

.PHONY: snapshot
snapshot:
	@# Checkout and build the common ancestor
	( [ -d $(GIT_DIVERGENCE_REF)$(SNAPSHOT_SUFFIX) ] || ( rm -rf *$(SNAPSHOT_SUFFIX) && $(MAKE) $(GIT_DIVERGENCE_REF)$(SNAPSHOT_SUFFIX) ) )
	rm -f base$(SNAPSHOT_SUFFIX) && ln -s $(GIT_DIVERGENCE_REF)$(SNAPSHOT_SUFFIX) base$(SNAPSHOT_SUFFIX)

.PHONY: characterize
characterize: .require-drift-tracelist snapshot
	( [ -d $(GIT_DIVERGENCE_REF)$(DRIFT_SUFFIX)/results ] || ( rm -rf *$(DRIFT_SUFFIX) && $(MAKE) $(GIT_DIVERGENCE_REF)$(DRIFT_SUFFIX) SNAPSHOT_DIR=$(realpath $(GIT_DIVERGENCE_REF)$(SNAPSHOT_SUFFIX)) ) )

.PHONY: compare-drift-all
compare-drift-all: .verify-drift-settings
	$(PROJECT_DIR)/install/bin/panthera report.reports.check_drift $(LATEST_DRIFT)/$(DRIFT_STUDY) $(GIT_DIVERGENCE_REF)$(DRIFT_SUFFIX)/$(DRIFT_STUDY) $(DRIFT_ARGS)

.PHONY: compare-drift
compare-drift:
	@# Compare drift (cycles) for existing results
	@$(MAKE) compare-drift-all DRIFT_ARGS="--show-outliers --method ratio --metric cycles"

.PHONY: latest-drift
latest-drift: .require-drift-tracelist install
	@# Run latest on DRIFT_TLIST for DRIFT_SPEC
	rm -rf $(LATEST_DRIFT)
	@$(MAKE) $(LATEST_DRIFT)

.PHONY: check-drift
check-drift: characterize
	@# Checkout common ancestor and run both on DRIFT_TLIST for DRIFT_SPEC and compare
	@[ "$$(grep -w $(DRIFT_SPEC) $(GIT_DIVERGENCE_REF)$(DRIFT_SUFFIX)/$(DRIFT_SPEC_TXT))" != "" ] || ( echo "Different DRIFT_SPEC used in $(GIT_DIVERGENCE_REF)$(DRIFT_SUFFIX) (remove old results)" && exit 1 )
	@[ "$$(grep -w $(DRIFT_TLIST) $(GIT_DIVERGENCE_REF)$(DRIFT_SUFFIX)/$(DRIFT_TLIST_TXT))" != "" ] || ( echo "Different DRIFT_TLIST used in $(GIT_DIVERGENCE_REF)$(DRIFT_SUFFIX) (remove old results)" && exit 1 )
	@$(MAKE) latest-drift
	( ( [ ! -s $(LATEST_DRIFT)/$(DRIFT_ERRORS) ] && [ -f $(LATEST_DRIFT)/$(DRIFT_ERRORS) ] ) || ( echo "Incomplete study or errors!"; cat $(LATEST_DRIFT)/$(DRIFT_ERRORS); exit 1 ) )
	@echo "Latest vs. $(GIT_DIVERGENCE_REF)"
	$(MAKE) compare-drift

.PHONY: check-drift-specs
check-drift-specs:
	@echo "Options for DRIFT_SPEC:"
	@ls $(PROJECT_DIR)/src/ampere/regress/drift_specifications | sed 's/.json//g' | sed 's/^/  /'


################################################################################
# Linting
################################################################################

.require-clean:
	git diff HEAD --quiet || ( echo "Run in a clean tree" && exit 1 )

# C++

.PHONY: .no-precompiled
.no-precompiled:
	@[ "$$(grep PRECOMPILE_SOME_HEADERS:BOOL=ON $(BUILD_DIRECTORY)/CMakeCache.txt)" == "" ] || ( echo "Run without precompiled headers" && exit 1 )

.PHONY: iwyu
iwyu: .clang-only .no-precompiled
	@# Include what you-use (check only)
	@$(MAKE) configure CMAKE_ARGS='-DINCLUDE_WHAT_YOU_USE=ON'
	# Adjust some stuff where iwyu suggests the less correct version of the header-file
	@$(MAKE) build SKIP_CONFIGURE=1 | \
		sed -e 's:<\(std.*\|assert\|limits\|time\|signal\|errno\|string\|math\|inttypes\|float\)\.h>:<c\1> :' | \
		sed -e 's:"\(fmt\|gtest\|gmock\)/\(.*\)":<\1/\2>:' | \
		grep -v '<bits/' | \
		tee iwyu.log

iwyu.log: iwyu

.PHONY: iwyu-fix
iwyu-fix: iwyu.log
	fix_includes.py --only_re='$(PROJECT_DIR)/src/ampere/.*' --nocomments < iwyu.log

.PHONY: .clang-tidy
.clang-tidy:
	@$(MAKE) configure CMAKE_ARGS='-DPRECOMPILE_SOME_HEADERS=OFF'
	@$(MAKE) build SKIP_CONFIGURE=1

.PHONY: clang-tidy
clang-tidy: .clang-only
	@# Clang tidy (check only), required checks
	@$(MAKE) configure CMAKE_ARGS='-DCLANG_TIDY=ON -DPRECOMPILE_SOME_HEADERS=OFF'
	@$(MAKE) build SKIP_CONFIGURE=1

.PHONY: clang-tidy-all
clang-tidy-all: .no-precompiled
	@# Clang tidy (check only), all checks
	@$(MAKE) .clang-tidy
	$(FIND_AMPERE_SRCS) $(CPP_FILES) | parallel -j $(LINT_JOBS) clang-tidy -p $(BUILD_DIRECTORY) $(CLANG_TIDY_ARGS)

.PHONY: clang-tidy-fix
clang-tidy-all-fix: .require-clean .no-precompiled
	@$(MAKE) .clang-tidy
	$(FIND_AMPERE_SRCS) $(CPP_FILES) | parallel -j $(LINT_JOBS) clang-tidy -p $(BUILD_DIRECTORY) --fix --fix-errors $(CLANG_TIDY_ARGS)

.PHONY: clang-tidy-fix-%
clang-tidy-fix-%: .require-clean .no-precompiled
	@# Fix specified clang-tidy check
	@$(MAKE) .clang-tidy
	$(FIND_AMPERE_SRCS) $(CPP_FILES) | parallel -j $(LINT_JOBS) clang-tidy -p $(BUILD_DIRECTORY) --fix --config='' --checks=$*

# Python

.PHONY: python-lint
python-lint:
	@# Python lint (check only)
	$(FIND_AMPERE_SRCS) $(PY_LINT_FILES) | xargs -r ruff check $(RUFF_ARGS)

.PHONY: python-lint-fix
python-lint-fix: .require-clean
	$(FIND_AMPERE_SRCS) $(PY_LINT_FILES) | xargs -r ruff check --fix $(RUFF_ARGS)


################################################################################
# Formatting
################################################################################

# Text

.PHONY: text-format
text-format:
	@# Fix text formatting
	echo "Add newlines..."
	$(PROJECT_DIR)/infrastructure/checks/newlines.sh --fix $(TEXT_FILES_REQUIRING_NEWLINES)
	echo "Replace tabs (only $(NON_SOURCE_TEXT_FILES))..."
	$(PROJECT_DIR)/infrastructure/checks/tabs_to_spaces.sh --fix $(NON_SOURCE_TEXT_FILES)

.PHONY: text-format-check
text-format-check:
	@# Check text formatting
	echo "Check newlines..."
	$(PROJECT_DIR)/infrastructure/checks/newlines.sh $(TEXT_FILES_REQUIRING_NEWLINES)
	echo "Check tabs..."
	$(PROJECT_DIR)/infrastructure/checks/tabs_to_spaces.sh $(TEXT_FILES_REQUIRING_SPACES)

# C++

.PHONY: .clang-format
.clang-format:
	$(FIND_AMPERE_SRCS) $(CPP_FILES) | parallel -j $(LINT_JOBS) $(CLANG_FORMAT) -i

.PHONY: clang-format
clang-format: .require-clean
	@# Fix C++ formatting
	$(MAKE) .clang-format

.PHONY: clang-format-changed
clang-format-changed:
	$(FIND_CHANGED_AMPERE_SRCS) $(CPP_FILES) | parallel -j $(LINT_JOBS) $(CLANG_FORMAT) -i

.PHONY: clang-format-check
clang-format-check:
	@# Check C++ formatting
	$(FIND_AMPERE_SRCS) $(CPP_FILES) | parallel -j $(LINT_JOBS) $(CLANG_FORMAT) -n -Werror

.PHONY: clang-format-check-changed
clang-format-check-changed:
	$(FIND_CHANGED_AMPERE_SRCS) $(CPP_FILES) | parallel -j $(LINT_JOBS) $(CLANG_FORMAT) -n -Werror

# Python

.PHONY: .python-format
.python-format:
	$(FIND_AMPERE_SRCS) $(PY_LINT_FILES) | xargs -r ruff check --select I --fix
	$(FIND_AMPERE_SRCS) $(PY_LINT_FILES) | xargs -r ruff format

.PHONY: python-format
python-format: .require-clean
	@# Fix Python formatting
	$(MAKE) .python-format

.PHONY: python-format-changed
python-format-changed:
	$(FIND_CHANGED_AMPERE_SRCS) $(PY_LINT_FILES) | xargs -r ruff check --select I --fix
	$(FIND_CHANGED_AMPERE_SRCS) $(PY_LINT_FILES) | xargs -r ruff format

.PHONY: python-format-check
python-format-check:
	@# Check Python formatting
	$(FIND_AMPERE_SRCS) $(PY_LINT_FILES) | xargs -r ruff check --select I
	$(FIND_AMPERE_SRCS) $(PY_LINT_FILES) | xargs -r ruff format --check

.PHONY: python-format-check-changed
python-format-check-changed:
	$(FIND_CHANGED_AMPERE_SRCS) $(PY_LINT_FILES) | xargs -r ruff check --select I
	$(FIND_CHANGED_AMPERE_SRCS) $(PY_LINT_FILES) | xargs -r ruff format --check

# All

.PHONY: format
format: .require-clean
	@# Fix all formatting
	$(MAKE) text-format
	$(MAKE) .clang-format
	$(MAKE) .python-format


################################################################################
# Pre-MR Checks (required)
################################################################################

.PHONY: check-format
check-format:
	@# Check format
	$(MAKE) text-format-check
	echo "Check C++..."
	$(MAKE) clang-format-check
	echo "Check Python..."
	$(MAKE) python-format-check

.PHONY: check
check:
	@# Run all checks
	$(MAKE) check-format


################################################################################
# Documentation
################################################################################

.PHONY: doc
doc:
	@# Build documentation HTML
	$(PYTHON) -m mkdocs build


################################################################################
# Caching
################################################################################

.PHONY: ccache-stats
ccache-stats:
	$(CCACHE) -p | egrep 'cache_dir|remote_storage'
	@echo
	$(CCACHE) -s

.PHONY: ccache-settings
ccache-settings:
	$(CCACHE) -p

.PHONY: ccache-reset-stats
ccache-reset-stats:
	$(CCACHE) -z

.PHONY: ccache-debug
ccache-debug: clean
	export CCACHE_DIR=ccache
	export CCACHE_DEBUG=true
	export CCACHE_DEBUGDIR=ccache_debug
	@$(MAKE) build


################################################################################
# Cleanup
################################################################################

.PHONY: .reset-cache
.reset-cache:
	rm -f $(BUILD_DIRECTORY)/CMakeCache.txt

.PHONY: reset-cache-%
reset-cache-%:
	@# Clear CMake cache for specified build
	@$(MAKE) .reset-cache BUILD_TYPE=$*

.PHONY: reset-cache
reset-cache: reset-cache-$(DEFAULT_BUILD_TYPE)
	@# Clear CMake cache for default build

.PHONY: .clean
.clean:
	rm -rf $(BUILD_DIRECTORY)

.PHONY: clean-%
clean-%:
	@# Clean specified build
	@$(MAKE) .clean BUILD_TYPE=$*

.PHONY: clean-all
clean-all:
	@# Clean all builds
	rm -rf $(BUILD_DIRECTORY_PREFIX)*

.PHONY: clean
clean: clean-$(DEFAULT_BUILD_TYPE)
	@# Clean default build


################################################################################
# Help
################################################################################

.PHONY: help
help:
	@# Shop all help
	@echo "usage: $(MAKE) <target>"
	@echo "  Various build, test, and other utility targets"
	@echo
	@echo "TARGETS"
	@grep -P -A 1 "^(###|[a-z-%%]+:)" $(PROJECT_DIR)/Makefile \
		| grep -v -P "^--" \
		| grep -v -P "^###" \
		| sed -E 's/^\t+@#/ /' \
		| grep -v -P "^\t+" \
		| grep -v -P "^$$" \
		| grep -v -P "^default:" \
		| sed 's/^[^#]/  \0/' \
		| sed 's/:.*//' \
		| sed 's/# *\(.*\)$$/\n\1:/' \
		| sed 's/%/*/'

.PHONY: help-*
help-%:
	@# Filter help for matching targets
	@grep -P -A 1 "^(.*$*[a-z-%%]+:)" $(PROJECT_DIR)/Makefile \
		| grep -v -P "^--" \
		| sed -E 's/^\t+@#/ /' \
		| grep -v -P "^\t+" \
		| grep -v -P "^$$" \
		| sed 's/%/*/' \
		| sed 's/:.*//'
