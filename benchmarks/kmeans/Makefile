ROOT = ../..
TARGETS = kmeans
LIBS = pthread dl
ARGS =

include $(ROOT)/common.mk

bench:: debug/bin/kmeans
	causal --- debug/bin/kmeans $(ARGS)
