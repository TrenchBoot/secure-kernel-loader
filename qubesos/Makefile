.DEFAULT_GOAL = get-sources
.SECONDEXPANSION:

DIST ?= fc37
VERSION := $(shell cat version)

SRC_FILES := \
             $(VERSION).tar.gz \

BUILDER_DIR ?= ../..
SRC_DIR ?= qubes-src

SRC_URLS := \
            https://github.com/TrenchBoot/secure-kernel-loader/archive/$(VERSION).tar.gz \

UNTRUSTED_SUFF := .UNTRUSTED

SHELL := bash

.PHONY: get-sources verify-sources clean clean-sources

ifeq ($(FETCH_CMD),)
$(error "You can not run this Makefile without having FETCH_CMD defined")
endif

%: %.sha512
	@$(FETCH_CMD) $@$(UNTRUSTED_SUFF) -- $(filter %/$@,$(SRC_URLS))
	@sha512sum --status -c <(printf "$$(cat $<)  -\n") <$@$(UNTRUSTED_SUFF) || \
		{ echo "Wrong SHA512 checksum on $@$(UNTRUSTED_SUFF)!"; exit 1; }
	@mv $@$(UNTRUSTED_SUFF) $@

get-sources: $(SRC_FILES)
	@true

verify-sources:
	@true

clean:
	@true

clean-sources:
	rm -f $(SRC_FILES) *$(UNTRUSTED_SUFF)
