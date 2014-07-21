ROOT = .
DIRS = lib tools benchmarks tests

include $(ROOT)/common.mk

bench: debug
	@$(MAKE) -C benchmarks bench

test: debug
	@$(MAKE) -C tests test
