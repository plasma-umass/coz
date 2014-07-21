ROOT = ../..
TARGET = inspect
INSTALL = 1
LIBS = causal_support dwarf++ elf++ boost_filesystem boost_system
INCLUDE_DIRS = $(ROOT)/lib/support/include \
							 $(ROOT)/deps/cppgoodies/include

include $(ROOT)/common.mk

CXXFLAGS += --std=c++11 -fPIC

test: debug
	./inspect $(ROOT)/tests/kmeans/kmeans
