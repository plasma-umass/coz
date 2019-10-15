ROOT    := ../..
TARGETS := linear_regression
LIBS    := -lpthread
CFLAGS  := -g -O2

include $(ROOT)/benchmark.mk

BENCH_LARGE_ARGS := linear_regression_datafiles/key_file_500MB.txt
BENCH_SMALL_ARGS := linear_regression_datafiles/key_file_50MB.txt

large_inputs: linear_regression_datafiles/key_file_500MB.txt

small_inputs: linear_regression_datafiles/key_file_50MB.txt

linear_regression_datafiles/%:
	@echo $(LOG_PREFIX) Downloading linear_regression inputs $(LOG_SUFFIX)
	@wget -c http://csl.stanford.edu/~christos/data/linear_regression.tar.gz
	@echo $(LOG_PREFIX) Unpacking linear_regression inputs $(LOG_SUFFIX)
	@tar xzf linear_regression.tar.gz
	@rm linear_regression.tar.gz
