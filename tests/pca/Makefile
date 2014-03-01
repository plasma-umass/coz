ROOT = ../..
TARGETS = pca
LIBS = pthread dl
ARGS = > /dev/null

include $(ROOT)/common.mk

test:: pca
	LD_PRELOAD=$(ROOT)/libcausal.so ./pca $(ARGS)
