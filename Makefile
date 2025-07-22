# Makefile
#
# Copyright (C) 2025, Charles Chiou

MAKEFLAGS =	--no-print-dir

TARGETS +=	build/meshmon

.PHONY: default clean distclean $(TARGETS)

default: $(TARGETS)

clean:
	@test -f build/Makefile && $(MAKE) -C build clean

distclean:
	rm -rf build

.PHONY: meshmon

meshmon: build/meshmon

build/meshmon: build/Makefile
	@$(MAKE) -C build

build/Makefile: CMakeLists.txt
	@mkdir -p build
	@cd build && cmake ..
