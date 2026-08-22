# Makefile
#
# Copyright (C) 2025, Charles Chiou

ARCH :=		$(shell uname -m)
MAKEFLAGS =	--no-print-dir

TARGETS +=	build/$(ARCH)/meshmon

.PHONY: default clean distclean

default: $(TARGETS)

clean:
	@test -f build/$(ARCH)/Makefile && $(MAKE) -C build/$(ARCH) clean

distclean:
	rm -rf build/

.PHONY: meshmon

meshmon: build/$(ARCH)/meshmon

MESHMON_TREE :=	\
	CMakeLists.txt version.h.in \
	$(wildcard *.cxx) $(wildcard *.hxx) \
	libmeshtastic
MESHMON_SRCS :=	$(shell find -H $(MESHMON_TREE) -type f \
	    \( -name '*.c' -o -name '*.cxx' -o -name '*.h' -o -name '*.hxx' \
	       -o -name 'CMakeLists.txt' -o -name 'version.h.in' \) \
	    2>/dev/null)

build/$(ARCH)/meshmon: build/$(ARCH)/Makefile $(MESHMON_SRCS)
	@if [ -f $@ ]; then \
		rm -f build/$(ARCH)/version.h; \
	fi
	@$(MAKE) -C build/$(ARCH)

build/$(ARCH)/Makefile: CMakeLists.txt
	@mkdir -p build/$(ARCH)
	@cd build/$(ARCH) && cmake ../..

.PHONY: release

release: build/$(ARCH)/Makefile
	@rm -f build/$(ARCH)/version.h
	@$(MAKE) -C build/$(ARCH)
