ROOT = ../..
TARGET = producer_consumer
LIBS = pthread dl
ARGS =
TEST_ARGS = 

include $(ROOT)/common.mk

CFLAGS += -O3
CXXFLAGS += -O3

bench:: debug/bin/producer_consumer
	$(ROOT)/release/bin/causal --- debug/bin/producer_consumer $(ARGS)

test:: debug/bin/producer_consumer
	$(ROOT)/debug/bin/causal --- debug/bin/producer_consumer $(TEST_ARGS)
