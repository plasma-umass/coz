ROOT    := ../..
TARGETS := histogram
LIBS    := pthread
CFLAGS  := -g -O2

include $(ROOT)/benchmark.mk

BENCH_LARGE_ARGS := histogram_datafiles/large.bmp
BENCH_SMALL_ARGS  := histogram_datafiles/small.bmp

large_inputs: histogram_datafiles/large.bmp

small_inputs: histogram_datafiles/small.bmp

histogram_datafiles/%:
	@echo $(LOG_PREFIX) Downloading histogram inputs $(LOG_SUFFIX)
	@wget -c http://csl.stanford.edu/~christos/data/histogram.tar.gz
	@echo $(LOG_PREFIX) Unpacking histogram inputs $(LOG_SUFFIX)
	@tar xzf histogram.tar.gz
	@rm histogram.tar.gz
