ROOT = ../..
TARGETS = libcausal.so
INSTALL = 1
LIBS = pthread dl rt causal_support dwarf++ elf++ boost_filesystem boost_system
INCLUDE_DIRS = $(ROOT)/lib/support/include\
							 $(ROOT)/deps/cppgoodies/include

CFLAGS =

include $(ROOT)/common.mk

CXXFLAGS += --std=c++11 -fPIC
