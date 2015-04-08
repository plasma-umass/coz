ROOT 		 := ..
TARGETS  := inspect
LIBS     := dl rt pthread
CXXFLAGS := --std=c++11 -g -O2
LDFLAGS  := $(ROOT)/deps/libelfin/elf/libelf++.a $(ROOT)/deps/libelfin/dwarf/libdwarf++.a
PREREQS  := $(ROOT)/deps/ccutil $(ROOT)/deps/libelfin

include $(ROOT)/common.mk
