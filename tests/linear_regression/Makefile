ROOT = ../..
TARGETS = linear_regression
LIBS = pthread
ARGS = linear_regression_datafiles/key_file_500MB.txt

include $(ROOT)/common.mk

setup: linear_regression_datafiles/key_file_500MB.txt

linear_regression_datafiles/%:
	wget http://mapreduce.stanford.edu/datafiles/linear_regression.tar.gz
	tar xvzf linear_regression.tar.gz
	rm linear_regression.tar.gz
