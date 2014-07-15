ROOT = ../..
TARGET = linear_regression
LIBS = pthread dl
ARGS = linear_regression_datafiles/key_file_500MB.txt
TEST_ARGS = linear_regression_datafiles/key_file_50MB.txt

include $(ROOT)/common.mk

setup: linear_regression_datafiles/key_file_50MB.txt

linear_regression_datafiles/%:
	wget http://mapreduce.stanford.edu/datafiles/linear_regression.tar.gz
	tar xvzf linear_regression.tar.gz
	rm linear_regression.tar.gz

bench:: debug/bin/linear_regression setup
	$(ROOT)/release/bin/causal --- debug/bin/linear_regression $(ARGS)

test:: debug/bin/linear_regression setup
	$(ROOT)/debug/bin/causal --- debug/bin/linear_regression $(TEST_ARGS)
