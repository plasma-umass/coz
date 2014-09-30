ROOT 	  := ../..
TARGETS := kmeans
LIBS    := pthread dl
CFLAGS  := -g -O2 -I$(ROOT)/include

BENCHMARK := 1

include $(ROOT)/common.mk

BENCH_ARGS := -d 3 -c 100 -p 100000 -s 1000
TEST_ARGS  := -d 3 -c 100 -p 10000 -s 100
