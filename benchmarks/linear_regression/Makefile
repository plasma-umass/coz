ROOT    := ../..
TARGETS := linear_regression
LIBS    := pthread dl
CFLAGS  := -g -O2

ifeq ($(USE_SYSTEM_COZ),)
CFLAGS += -I$(ROOT)/include
endif

BENCHMARK := 1

include $(ROOT)/common.mk

BENCH_ARGS := linear_regression_datafiles/key_file_500MB.txt
TEST_ARGS  := linear_regression_datafiles/key_file_50MB.txt

bench_inputs: linear_regression_datafiles/key_file_500MB.txt

test_inputs: linear_regression_datafiles/key_file_50MB.txt

linear_regression_datafiles/%:
	@echo $(LOG_PREFIX) Downloading linear_regression inputs $(LOG_SUFFIX)
	@wget -c http://csl.stanford.edu/~christos/data/linear_regression.tar.gz
	@echo $(LOG_PREFIX) Unpacking linear_regression inputs $(LOG_SUFFIX)
	@tar xzf linear_regression.tar.gz
	@rm linear_regression.tar.gz
