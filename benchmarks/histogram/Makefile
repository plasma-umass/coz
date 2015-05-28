ROOT 	  := ../..
TARGETS := histogram
LIBS    := pthread dl
CFLAGS  := -g -O2 -I$(ROOT)/include

BENCHMARK := 1

include $(ROOT)/common.mk

BENCH_ARGS := histogram_datafiles/large.bmp
TEST_ARGS  := histogram_datafiles/small.bmp

bench_inputs: histogram_datafiles/large.bmp

test_inputs: histogram_datafiles/small.bmp

histogram_datafiles/%:
	@echo $(LOG_PREFIX) Downloading histogram inputs $(LOG_SUFFIX)
	@wget -c http://csl.stanford.edu/~christos/data/histogram.tar.gz
	@echo $(LOG_PREFIX) Unpacking histogram inputs $(LOG_SUFFIX)
	@tar xzf histogram.tar.gz
	@rm histogram.tar.gz
