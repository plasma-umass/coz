ROOT = ../..
TARGETS = producer_consumer
LIBS = pthread dl
ARGS =

include $(ROOT)/common.mk

test:: producer_consumer
	LD_PRELOAD=$(ROOT)/libcausal.so ./producer_consumer $(ARGS)
