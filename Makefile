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
	@cp $(install_files) $(prefix)/bin

uninstall:
	@for f in $(install_files); do		\
		$(RM) $(prefix)/bin/$$f;	\
	done
