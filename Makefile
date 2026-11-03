# Makefile
#
# Copyright (C) 2025, Charles Chiou

ARCH :=		$(shell uname -m)
MAKEFLAGS =	--no-print-dir
CMAKE :=	$(shell [ -x /usr/bin/cmake ] && echo /usr/bin/cmake || which cmake)
NUM_PROCS :=	$(shell nproc 2>/dev/null || echo 4)

ifneq ($(findstring mull-ir-frontend,$(CXXFLAGS)),)
BUILD_DIR :=	build-mull/$(ARCH)
GCC_INSTALL_DIR := $(shell dirname "$$(g++ -print-libgcc-file-name)" 2>/dev/null)
CMAKE_SAN_FLAGS := -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_C_FLAGS="--gcc-install-dir=$(GCC_INSTALL_DIR) $(CFLAGS)" -DCMAKE_CXX_FLAGS="--gcc-install-dir=$(GCC_INSTALL_DIR) $(CXXFLAGS)" -DCMAKE_EXE_LINKER_FLAGS="--gcc-install-dir=$(GCC_INSTALL_DIR) $(LDFLAGS)"
else ifneq ($(strip $(CFLAGS)$(CXXFLAGS)$(LDFLAGS)),)
BUILD_DIR :=	build-sanitized/$(ARCH)
CMAKE_SAN_FLAGS := -DCMAKE_C_FLAGS="$(CFLAGS)" -DCMAKE_CXX_FLAGS="$(CXXFLAGS)" -DCMAKE_EXE_LINKER_FLAGS="$(LDFLAGS)"
else
BUILD_DIR :=	build/$(ARCH)
CMAKE_SAN_FLAGS :=
endif

TARGETS +=	$(BUILD_DIR)/meshmon

.PHONY: default clean distclean all test submodules

all: default

default: submodules $(TARGETS)

submodules:
	@if [ -f .gitmodules ] && { [ ! -f third_party/json/include/nlohmann/json.hpp ] || [ ! -f third_party/cpp-httplib/httplib.h ]; }; then \
		git submodule update --init --recursive; \
	fi

test: all
	@$(CMAKE) --build $(BUILD_DIR) --target test -j$(NUM_PROCS)

clean:
	@test -f $(BUILD_DIR)/Makefile && $(MAKE) -C $(BUILD_DIR) clean || true

distclean:
	rm -rf build/ build-sanitized/ build-mull/

.PHONY: meshmon

meshmon: $(BUILD_DIR)/meshmon

MESHMON_TREE :=	\
	CMakeLists.txt version.h.in \
	$(wildcard *.cxx) $(wildcard *.hxx) \
	libmeshtastic
MESHMON_SRCS :=	$(shell find -H $(MESHMON_TREE) -type f \
	    \( -name '*.c' -o -name '*.cxx' -o -name '*.h' -o -name '*.hxx' \
	       -o -name 'CMakeLists.txt' -o -name 'version.h.in' \) \
	    2>/dev/null)

$(BUILD_DIR)/meshmon: $(BUILD_DIR)/Makefile $(MESHMON_SRCS)
	@if [ -f $@ ]; then \
		rm -f $(BUILD_DIR)/version.h; \
	fi
	@$(MAKE) -C $(BUILD_DIR) -j$(NUM_PROCS)

$(BUILD_DIR)/Makefile: CMakeLists.txt
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) ../.. $(CMAKE_SAN_FLAGS)

.PHONY: release

release: $(BUILD_DIR)/Makefile
	@rm -f $(BUILD_DIR)/version.h
	@$(MAKE) -C $(BUILD_DIR) -j$(NUM_PROCS)
