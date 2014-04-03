<<<<<<< HEAD
ROOT = ../..
TARGETS = histogram
LIBS = pthread dl
ARGS = histogram_datafiles/large.bmp
=======
TEST_ROOT = ..
LIBS = pthread
ARGS = histogram_datafiles/small.bmp
>>>>>>> 4d5628d162b7cdfa13645b696cfc187126b21929

include $(ROOT)/common.mk

setup: histogram_datafiles/small.bmp

histogram_datafiles/%:
	wget http://mapreduce.stanford.edu/datafiles/histogram.tar.gz
	tar xvzf histogram.tar.gz
	rm histogram.tar.gz

test:: histogram setup
	LD_PRELOAD=$(ROOT)/libcausal.so ./histogram $(ARGS)
	
