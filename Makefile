ROOT = .
DIRS = runtime

include $(ROOT)/common.mk

test: build
	@$(MAKE) -C tests test
