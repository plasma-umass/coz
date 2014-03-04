TEST_ROOT = ..
LIBS = pthread
ARGS = linear_regression_datafiles/key_file_50MB.txt

include $(TEST_ROOT)/common.mk

setup: linear_regression_datafiles/key_file_50MB.txt

linear_regression_datafiles/%:
	wget http://mapreduce.stanford.edu/datafiles/linear_regression.tar.gz
	tar xvzf linear_regression.tar.gz
	rm linear_regression.tar.gz
