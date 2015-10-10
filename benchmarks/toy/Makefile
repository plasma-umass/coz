ROOT     := ../..
TARGETS  := toy
LIBS     := dl pthread
CXXFLAGS := --std=c++11 -g -O2

ifeq ($(USE_SYSTEM_COZ),)
CFLAGS += -I$(ROOT)/include
endif

BENCHMARK := 1

include $(ROOT)/common.mk
