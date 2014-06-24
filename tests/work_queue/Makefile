ROOT = ../..
TARGETS = work_queue
LIBS = pthread dl
#ARGS = --causal-select-block _Z11work_item_Av 0

CFLAGS = -O3

include $(ROOT)/common.mk

test:: work_queue
	LD_PRELOAD=$(ROOT)/libcausal.so ./work_queue $(ARGS)
