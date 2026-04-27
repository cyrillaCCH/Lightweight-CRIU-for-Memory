#
# MTD Makefile - Follows CRIU cross-compilation conventions
#

CROSS_COMPILE ?=
CXX           ?= $(CROSS_COMPILE)g++
STRIP         ?= $(CROSS_COMPILE)strip

# Inherit ARCH from environment or try to detect it
ARCH ?= $(shell uname -m | sed -e s/i.86/x86/ -e s/x86_64/x86/ -e s/armv7l/arm/ -e s/aarch64/aarch64/)

CXXFLAGS += -O2 -Wall -std=c++17

# Architecture-specific flags (if needed)
ifeq ($(ARCH),aarch64)
    CXXFLAGS += -DCONFIG_AARCH64
endif
ifeq ($(ARCH),x86)
    CXXFLAGS += -DCONFIG_X86_64
endif

all: mtd

mtd: mtd.cpp mtd_proto.h
	$(CXX) $(CXXFLAGS) -o mtd mtd.cpp
	$(STRIP) mtd

clean:
	rm -f mtd

.PHONY: all clean
