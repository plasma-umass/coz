ROOT = ../..
TARGETS = producer_consumer
LIBS = pthread dl
ARGS =

include $(ROOT)/common.mk

bench:: debug/bin/producer_consumer
	causal --- debug/bin/producer_consumer $(ARGS)
