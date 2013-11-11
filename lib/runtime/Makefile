LEVEL = ../../../..
LIBRARYNAME = causal_rt
SOURCES = libcausal_rt.cpp
LIBS = dl

CXXFLAGS += -g -std=c++11

OS = $(shell uname -s)
ifeq ($(OS),Darwin)
CXXFLAGS += -stdlib=libc++ -nostdinc++ -I/usr/src/libcxx/include -L/usr/src/libcxx/lib
endif

include $(LEVEL)/Makefile.common
