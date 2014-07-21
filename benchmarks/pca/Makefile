ROOT = ../..
TARGET = pca
LIBS = pthread dl
ARGS = > /dev/null
TEST_ARGS = > /dev/null

include $(ROOT)/common.mk

bench:: debug/bin/pca
	$(ROOT)/release/bin/causal --- debug/bin/pca $(ARGS)

test:: debug/bin/pca
	$(ROOT)/debug/bin/causal --- debug/bin/pca $(TEST_ARGS)
