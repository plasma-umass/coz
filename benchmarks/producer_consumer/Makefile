ROOT = ../..
TARGETS = producer_consumer
LIBS = pthread dl
ARGS =

include $(ROOT)/common.mk

test:: debug/bin/producer_consumer
	LD_PRELOAD=$(ROOT)/debug/lib/libcausal.so ./debug/bin/producer_consumer $(ARGS)
