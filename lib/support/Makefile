ROOT = ../..
TARGET = libcausal_support.a
INSTALL = 1
LIBS = dwarf++ elf++ boost_filesystem boost_system
INCLUDE_DIRS = $(ROOT)/deps/cppgoodies/include \
							 $(ROOT)/deps/libelfin/dwarf\
							 $(ROOT)/deps/libelfin/elf

include $(ROOT)/common.mk

CXXFLAGS += --std=c++11 -fPIC
