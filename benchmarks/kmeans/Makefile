ROOT = ../..
TARGET = kmeans
LIBS = pthread dl
ARGS = -d 3 -c 100 -p 100000 -s 1000
TEST_ARGS = -d 3 -c 100 -p 10000 -s 100

include $(ROOT)/common.mk

bench:: debug/bin/kmeans
	$(ROOT)/release/bin/causal --- debug/bin/kmeans $(ARGS)

test:: debug/bin/kmeans
	$(ROOT)/debug/bin/causal --- debug/bin/kmeans $(TEST_ARGS)
