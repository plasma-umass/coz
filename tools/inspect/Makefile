ROOT = ../..
TARGETS = inspect
LIBS = dwarf++ elf++ boost_filesystem boost_system
INCLUDE_DIRS = $(ROOT)/runtime \
							 $(ROOT)/deps/cppgoodies/include \
							 $(ROOT)/deps/libelfin/dwarf \
							 $(ROOT)/deps/libelfin/elf

include $(ROOT)/common.mk

CXXFLAGS += --std=c++11 -fPIC

test: debug
	./inspect $(ROOT)/tests/kmeans/kmeans
