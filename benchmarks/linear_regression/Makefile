ROOT 	  := ../..
TARGETS := linear_regression
LIBS    := pthread dl
CFLAGS  := -g -O2 -I$(ROOT)/include

BENCHMARK := 1

include $(ROOT)/common.mk

BENCH_ARGS := linear_regression_datafiles/key_file_500MB.txt
TEST_ARGS  := linear_regression_datafiles/key_file_50MB.txt

bench_inputs: linear_regression_datafiles/key_file_500MB.txt

test_inputs: linear_regression_datafiles/key_file_50MB.txt

linear_regression_datafiles/%:
	@echo $(LOG_PREFIX) Downloading linear_regression inputs $(LOG_SUFFIX)
	@wget -c http://mapreduce.stanford.edu/datafiles/linear_regression.tar.gz
	@echo $(LOG_PREFIX) Unpacking linear_regression inputs $(LOG_SUFFIX)
	@tar xzf linear_regression.tar.gz
	@rm linear_regression.tar.gz
