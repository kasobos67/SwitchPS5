# ps5-native-app-boilerplate - Sandbox elevation helper build.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

ifdef PS5_PAYLOAD_SDK
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
include /opt/ps5-payload-sdk/toolchain/prospero.mk
endif

OUTPUT ?= ../../../build/sandbox-elevation/sandbox-elevator.elf
CXXFLAGS := -std=c++20 -O2 -g -Wall -Wextra -Werror
LDFLAGS := -Wl,--build-id=none

.PHONY: all clean

all: $(OUTPUT)

$(OUTPUT): main.cpp ../protocol.hpp
	@mkdir -p -- $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f -- $(OUTPUT)
