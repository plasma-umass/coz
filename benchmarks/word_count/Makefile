ROOT    := ../..
TARGETS := word_count
LIBS    := pthread
CFLAGS  := -g -O2

include $(ROOT)/benchmark.mk

BENCH_LARGE_ARGS := word_count_datafiles/word_100MB.txt
BENCH_SMALL_ARGS := word_count_datafiles/word_10MB.txt

large_inputs: word_count_datafiles/word_100MB.txt

small_inputs: word_count_datafiles/word_10MB.txt

word_count_datafiles/%:
	@echo $(LOG_PREFIX) Downloading word_count inputs $(LOG_SUFFIX)
	@wget -c http://csl.stanford.edu/~christos/data/word_count.tar.gz
	@echo $(LOG_PREFIX) Unpacking word_count inputs $(LOG_SUFFIX)
	@tar xzf word_count.tar.gz
	@rm word_count.tar.gz
