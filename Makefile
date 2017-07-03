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

CFLAGS=-Wall -W -g -O2

ARCH_SUPPORTED:=$(shell echo -e "\n\#if !(defined(_ARCH_PPC64) && defined(_LITTLE_ENDIAN))"\
	"\n\#error \"This tool is only supported on ppc64le architecture\""\
	"\n\#endif" | ($(CC) $(CFLAGS) -E -o /dev/null - 2>&1 || exit 1))

ifneq ($(strip $(ARCH_SUPPORTED)),)
$(error Target not supported. Currently CAPI utils is only supported on ppc64le)
endif

prefix=/usr/local

TARGETS=capi-flash-AlphaData7v3 capi-flash-AlphaDataKU60 capi-flash-NallatechKU60 capi-flash-AlphaDataKU115 capi-flash-Nallatech

install_files = $(TARGETS) capi-flash-script.sh psl-devices

.PHONY: all 
all: $(TARGETS)

capi-flash-AlphaData7v3: src/capi_flash_ad7v3ku3_user.c
	$(CC) $(CFLAGS) $< -o $@

capi-flash-AlphaDataKU60: src/capi_flash_ad7v3ku3_user.c
	$(CC) $(CFLAGS) $< -o $@

capi-flash-NallatechKU60: src/capi_flash_ad7v3ku3_user.c
	$(CC) $(CFLAGS) $< -o $@

capi-flash-AlphaDataKU115: src/capi_flash_adku115_user.c
	$(CC) $(CFLAGS) $< -o $@

capi-flash-Nallatech: src/capi_flash_nallatech_user.c
	$(CC) $(CFLAGS) $< -o $@

.PHONY: install
install: $(TARGETS)
	@chmod a+x capi-flash-*
	@mkdir -p $(prefix)/capi-utils
	@cp $(install_files) $(prefix)/capi-utils
	@ln -sf $(prefix)/capi-utils/capi-flash-script.sh \
		$(prefix)/bin/capi-flash-script

.PHONY: uninstall
uninstall:
	@rm -rf $(prefix)/capi-utils
	@rm -f $(prefix)/bin/capi-flash-script

.PHONY: clean
clean:
	@rm -rf $(TARGETS)

