#
# Makefile
#   to help simlifying things
#

prefix=/usr/local

install_files = $(wildcard capi-flash-*) psl-devices

all:
	@echo "Nothing to do"

install:
	@chmod a+x capi-flash-*
	@mkdir -p $(prefix)/capi-utils
	@cp $(install_files) $(prefix)/capi-utils
	@ln -s $(prefix)/capi-utils/capi-flash-script.sh \
		$(prefix)/bin/capi-flash-script

uninstall:
	@rm -rf $(prefix)/capi-utils
	@rm $(prefix)/bin/capi-flash-script

