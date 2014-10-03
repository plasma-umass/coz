ROOT 		 := ..
TARGETS  := libcausal.so
LIBS     := dl dwarf++ elf++ rt pthread
CXXFLAGS := --std=c++11 -g -O2 -fPIC -I../include -Iinclude
PREREQS  := include/ccutil

include $(ROOT)/common.mk

include/ccutil:
	@echo $(LOG_PREFIX) Checking out ccutil includes $(LOG_SUFFIX)
	@git clone git://github.com/ccurtsinger/ccutil include/ccutil
