ROOT = ..
TARGETS = $(ROOT)/libcausal.so
LIBS = pthread dl rt udis86 dwarf++ elf++
INCLUDE_DIRS = $(ROOT)/deps/cppgoodies/include \
							 $(ROOT)/deps/libelfin/dwarf\
							 $(ROOT)/deps/libelfin/elf

CFLAGS =

include $(ROOT)/common.mk

CXXFLAGS += --std=c++11 -fPIC
