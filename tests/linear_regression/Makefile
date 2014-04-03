<<<<<<< HEAD
ROOT = ../..
TARGETS = linear_regression
LIBS = pthread dl
ARGS = linear_regression_datafiles/key_file_500MB.txt
=======
TEST_ROOT = ..
LIBS = pthread
ARGS = linear_regression_datafiles/key_file_50MB.txt
>>>>>>> 4d5628d162b7cdfa13645b696cfc187126b21929

include $(ROOT)/common.mk

setup: linear_regression_datafiles/key_file_50MB.txt

linear_regression_datafiles/%:
	wget http://mapreduce.stanford.edu/datafiles/linear_regression.tar.gz
	tar xvzf linear_regression.tar.gz
	rm linear_regression.tar.gz

test:: linear_regression setup
	LD_PRELOAD=$(ROOT)/libcausal.so ./linear_regression $(ARGS)
