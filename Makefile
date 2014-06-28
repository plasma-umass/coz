ROOT = .
DIRS = lib tools

include $(ROOT)/common.mk

test: debug
	@$(MAKE) -C tests test
