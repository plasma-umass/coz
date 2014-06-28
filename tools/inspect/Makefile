ROOT = ../..
TARGETS = inspect
INSTALL = 1
LIBS = dwarf++ elf++ boost_filesystem boost_system
INCLUDE_DIRS = $(ROOT)/lib/runtime/include \
							 $(ROOT)/deps/cppgoodies/include \
							 $(ROOT)/deps/libelfin/dwarf \
							 $(ROOT)/deps/libelfin/elf

include $(ROOT)/common.mk

CXXFLAGS += --std=c++11 -fPIC

test: debug
	./inspect $(ROOT)/tests/kmeans/kmeans
