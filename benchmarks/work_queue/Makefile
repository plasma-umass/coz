ROOT = ../..
TARGETS = work_queue
LIBS = pthread dl
#ARGS = --causal-select-block _Z11work_item_Av 0

CFLAGS = -O3

include $(ROOT)/common.mk

test:: debug/bin/work_queue
	LD_PRELOAD=$(ROOT)/debug/lib/libcausal.so ./debug/bin/work_queue $(ARGS)
