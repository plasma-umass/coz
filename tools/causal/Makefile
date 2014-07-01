ROOT = ../..
TARGETS = causal
INSTALL = 1
LIBS = boost_program_options
INCLUDE_DIRS = $(ROOT)/lib/runtime/include

include $(ROOT)/common.mk

ROOT_PATH = $(shell pwd)/$(ROOT)

CXXFLAGS += --std=c++11 -fPIC -DCAUSAL_ROOT_PATH=\"$(ROOT_PATH)\"
