ROOT = ../..
TARGETS = unbalanced
LIBS = pthread dl
ARGS =

include $(ROOT)/common.mk

test:: unbalanced
	LD_PRELOAD=$(ROOT)/libcausal.so ./unbalanced $(ARGS)
