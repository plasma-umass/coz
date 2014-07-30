ROOT = ../..
TARGET = inspect
INSTALL = 1
LIBS = causal_support dwarf++ elf++ boost_filesystem boost_system
INCLUDE_DIRS = $(ROOT)/lib/support/include \
							 $(ROOT)/deps/cppgoodies/include

include $(ROOT)/common.mk

CXXFLAGS += --std=c++11 -fPIC

debug:: $(ROOT)/lib/support/debug/lib/libcausal_support.a

release:: $(ROOT)/lib/support/release/lib/libcausal_support.a

test: debug
	./debug/bin/inspect $(ROOT)/benchmarks/producer_consumer/debug/bin/producer_consumer
