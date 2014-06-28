ROOT = ../..
TARGETS = libcausal_support.a
LIBS = pthread rt udis86 dwarf++ elf++
INCLUDE_DIRS = $(ROOT)/deps/cppgoodies/include \
							 $(ROOT)/deps/libelfin/dwarf\
							 $(ROOT)/deps/libelfin/elf

include $(ROOT)/common.mk

CXXFLAGS += --std=c++11 -fPIC
