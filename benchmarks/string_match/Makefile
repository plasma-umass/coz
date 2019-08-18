ROOT    := ../..
TARGETS := string_match
LIBS    := pthread
CFLAGS  := -g -O2

include $(ROOT)/benchmark.mk

BENCH_LARGE_ARGS := string_match_datafiles/key_file_500MB.txt > /dev/null
BENCH_SMALL_ARGS := string_match_datafiles/key_file_50MB.txt > /dev/null

large_inputs: string_match_datafiles/key_file_500MB.txt

small_inputs: string_match_datafiles/key_file_50MB.txt

string_match_datafiles/%:
	@echo $(LOG_PREFIX) Downloading string_match inputs $(LOG_SUFFIX)
	@wget -c http://csl.stanford.edu/~christos/data/string_match.tar.gz
	@echo $(LOG_PREFIX) Unpacking string_match inputs $(LOG_SUFFIX)
	@tar xzf string_match.tar.gz
	@rm string_match.tar.gz
