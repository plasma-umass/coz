ROOT = ../..
TARGETS = pca
LIBS = pthread dl
ARGS = > /dev/null

include $(ROOT)/common.mk

test:: debug/bin/pca
	LD_PRELOAD=$(ROOT)/debug/lib/libcausal.so ./debug/bin/pca $(ARGS)
