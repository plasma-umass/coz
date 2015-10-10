ROOT     := ../..
TARGETS  := producer_consumer
LIBS     := dl pthread
CXXFLAGS := -g -O2

ifeq ($(USE_SYSTEM_COZ),)
CXXFLAGS += -I$(ROOT)/include
endif

BENCHMARK := 1

include $(ROOT)/common.mk
