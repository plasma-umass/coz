ROOT = ../..
TARGET = libcausal.so
INSTALL = 1
LIBS = pthread dl rt causal_support dwarf++ elf++ boost_filesystem boost_program_options boost_system tbb
INCLUDE_DIRS = $(ROOT)/lib/support/include\
							 $(ROOT)/deps/cppgoodies/include

CFLAGS =

include $(ROOT)/common.mk

debug/lib/libcausal.so:: $(ROOT)/debug/lib/libcausal_support.a

release/lib/libcausal.so:: $(ROOT)/release/lib/libcausal_support.a

CXXFLAGS += --std=c++11 -fPIC
