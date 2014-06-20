ROOT = .
DIRS = runtime tools

include $(ROOT)/common.mk

test: build
	@$(MAKE) -C tests test
