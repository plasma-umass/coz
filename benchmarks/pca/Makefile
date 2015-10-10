ROOT    := ../..
TARGETS := pca
LIBS    := pthread dl
CFLAGS  := -g -O2

ifeq ($(USE_SYSTEM_COZ),)
CFLAGS += -I$(ROOT)/include
endif

BENCHMARK := 1

include $(ROOT)/common.mk

BENCH_ARGS := > /dev/null
TEST_ARGS  := > /dev/null
