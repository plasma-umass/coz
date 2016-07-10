ROOT 		 := ..
TARGETS  := libcoz.so
LIBS     := dl rt pthread
CXXFLAGS := --std=c++11 -g -O2 -fPIC -I$(ROOT)/include
#LDFLAGS  := -L$(ROOT)/deps/libelfin/elf -L$(ROOT)/deps/libelfin/dwarf -Wl,--whole-archive -lelf++ -ldwarf++ -Wl,--no-whole-archive
PREREQS  := $(ROOT)/deps/ccutil $(ROOT)/deps/libelfin

include $(ROOT)/common.mk

install::
	$(INSTALL) -D $(ROOT)/libcoz/libcoz.so $(DESTDIR)$(pkglibdir)/libcoz.so
