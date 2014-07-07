ROOT = ../..
TARGETS = pca
LIBS = pthread dl
ARGS = > /dev/null

include $(ROOT)/common.mk

bench:: debug/bin/pca
	causal --- debug/bin/pca $(ARGS)
