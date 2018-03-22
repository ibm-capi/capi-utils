#
# Copyright 2016 International Business Machines
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
ifneq ($(CROSS_COMPILE),)
CC=$(CROSS_COMPILE)gcc
endif

ifneq ($(prefix),)
prefix=/usr/local
endif

HAS_GIT = $(shell git describe > /dev/null 2>&1 && echo y || echo n)
VERSION=0.1.2
ifeq (${HAS_GIT},y)
	GIT_BRANCH=$(shell git describe --abbrev=4 --always --tags | sed -e 's/v//g')
	VERSION:=$(VERSION)-$(GIT_BRANCH)
endif

CFLAGS=-Wall -W -g -O2 -I./include -DGIT_VERSION=\"$(VERSION)\"

ARCH_SUPPORTED:=$(shell echo -e "\n\#if !(defined(_ARCH_PPC64) && defined(_LITTLE_ENDIAN))"\
	"\n\#error \"This tool is only supported on ppc64le architecture\""\
	"\n\#endif" | ($(CC) $(CFLAGS) -E -o /dev/null - 2>&1 || exit 1))

ifneq ($(strip $(ARCH_SUPPORTED)),)
$(error Target not supported. Currently CAPI utils is only supported on ppc64le)
endif

install_point=lib/capi-utils

TARGETS=capi-flash

install_files = $(TARGETS) capi-utils-common.sh capi-flash-script.sh capi-reset.sh psl-devices

.PHONY: all 
all: $(TARGETS)

capi-flash: src/capi_flash.c
	$(CC) $(CFLAGS) $< -o $@

.PHONY: install
install: $(TARGETS)
	@chmod a+x capi-flash-*
	@mkdir -p $(prefix)/$(install_point)
	@cp $(install_files) $(prefix)/$(install_point)
	@ln -sf $(prefix)/$(install_point)/capi-flash-script.sh \
		$(prefix)/bin/capi-flash-script
	@ln -sf $(prefix)/$(install_point)/capi-reset.sh \
		$(prefix)/bin/capi-reset

.PHONY: uninstall
uninstall:
	@rm -rf $(prefix)/$(install_point)
	@rm -f $(prefix)/bin/capi-flash-script
	@rm -f $(prefix)/bin/capi-reset

.PHONY: clean
clean:
	@rm -rf $(TARGETS)

