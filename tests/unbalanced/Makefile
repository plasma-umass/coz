ROOT = ../..
TARGETS = unbalanced
LIBS = pthread dl
ARGS =

include $(ROOT)/common.mk

test:: debug/bin/unbalanced
	LD_PRELOAD=$(ROOT)/debug/lib/libcausal.so ./debug/bin/unbalanced $(ARGS)
