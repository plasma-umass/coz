ROOT = ../..
TARGETS = work_queue
LIBS = pthread dl
ARGS =

include $(ROOT)/common.mk

test:: work_queue
	LD_PRELOAD=$(ROOT)/libcausal.so ./work_queue $(ARGS)
