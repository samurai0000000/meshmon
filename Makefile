# Makefile
#
# Copyright (C) 2025, Charles Chiou

ARCH :=		$(shell uname -m)
MAKEFLAGS =	--no-print-dir

TARGETS +=	build/$(ARCH)/meshmon

.PHONY: default clean distclean $(TARGETS)

default: $(TARGETS)

clean:
	@test -f build/$(ARCH)/Makefile && $(MAKE) -C build/$(ARCH) clean

distclean:
	rm -rf build/

.PHONY: meshmon

meshmon: build/$(ARCH)/meshmon

build/$(ARCH)/meshmon: build/$(ARCH)/Makefile
	@$(MAKE) -C build/$(ARCH)

build/$(ARCH)/Makefile: CMakeLists.txt
	@mkdir -p build/$(ARCH)
	@cd build/$(ARCH) && cmake ../..

.PHONY: release

release: build/$(ARCH)/Makefile
	@rm -f build/$(ARCH)/version.h
	@$(MAKE) -C build/$(ARCH)
