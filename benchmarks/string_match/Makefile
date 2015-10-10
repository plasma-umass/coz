ROOT    := ../..
TARGETS := string_match
LIBS    := pthread dl
CFLAGS  := -g -O2

ifeq ($(USE_SYSTEM_COZ),)
CFLAGS += -I$(ROOT)/include
endif

BENCHMARK := 1

include $(ROOT)/common.mk

BENCH_ARGS := string_match_datafiles/key_file_500MB.txt > /dev/null
TEST_ARGS  := string_match_datafiles/key_file_50MB.txt > /dev/null

bench_inputs: string_match_datafiles/key_file_500MB.txt

test_inputs: string_match_datafiles/key_file_50MB.txt

string_match_datafiles/%:
	@echo $(LOG_PREFIX) Downloading string_match inputs $(LOG_SUFFIX)
	@wget -c http://csl.stanford.edu/~christos/data/reverse_index.tar.gz
	@echo $(LOG_PREFIX) Unpacking string_match inputs $(LOG_SUFFIX)
	@tar xzf string_match.tar.gz
	@rm string_match.tar.gz
