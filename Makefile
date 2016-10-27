CC=gcc

ARCH:= $(shell uname -p)
ifneq ($(ARCH),ppc64le)
  $(error $(ARCH) does not support CAPI)
endif

TARGETS=capi-flash-AlphaData7v3 capi-flash-AlphaDataKU60 capi-flash-AlphaDataKU115 capi-flash-Nallatech

.PHONY: all
all: $(TARGETS)

capi-flash-AlphaData7v3: src/capi_flash_ad7v3ku3_user.c
	$(CC) $< -o $@

capi-flash-AlphaDataKU60: src/capi_flash_ad7v3ku3_user.c
	$(CC) $< -o $@

capi-flash-AlphaDataKU115: src/capi_flash_adku115_user.c
	$(CC) $< -o $@

capi-flash-Nallatech: src/capi_flash_nallatech_user.c
	$(CC) $< -o $@

.PHONY: clean
clean:
	@rm -rf $(TARGETS)

