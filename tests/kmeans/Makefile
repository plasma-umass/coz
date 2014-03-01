ROOT = ../..
TARGETS = kmeans
LIBS = pthread dl
ARGS =

include $(ROOT)/common.mk

test:: kmeans
	LD_PRELOAD=$(ROOT)/libcausal.so ./kmeans $(ARGS)
