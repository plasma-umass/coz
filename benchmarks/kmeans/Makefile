ROOT = ../..
TARGETS = kmeans
LIBS = pthread dl
ARGS =

include $(ROOT)/common.mk

test:: debug/bin/kmeans
	LD_PRELOAD=$(ROOT)/debug/lib/libcausal.so ./debug/bin/kmeans $(ARGS)
