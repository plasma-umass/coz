ROOT 	  := ../..
TARGETS := word_count
LIBS    := pthread dl
CFLAGS  := -g -O2 -I$(ROOT)/include

BENCHMARK := 1

include $(ROOT)/common.mk

BENCH_ARGS := word_count_datafiles/word_100MB.txt
TEST_ARGS  := word_count_datafiles/word_10MB.txt

bench_inputs: word_count_datafiles/word_100MB.txt

test_inputs: word_count_datafiles/word_10MB.txt

word_count_datafiles/%:
	@echo $(LOG_PREFIX) Downloading word_count inputs $(LOG_SUFFIX)
	@wget -c http://csl.stanford.edu/~christos/data/word_count.tar.gz
	@echo $(LOG_PREFIX) Unpacking word_count inputs $(LOG_SUFFIX)
	@tar xzf word_count.tar.gz
	@rm word_count.tar.gz
