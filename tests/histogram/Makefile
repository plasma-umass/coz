ROOT = ../..
TARGETS = histogram
LIBS = pthread dl
ARGS = histogram_datafiles/large.bmp

include $(ROOT)/common.mk

setup: histogram_datafiles/large.bmp

histogram_datafiles/%:
	wget http://mapreduce.stanford.edu/datafiles/histogram.tar.gz
	tar xvzf histogram.tar.gz
	rm histogram.tar.gz

test:: histogram setup
	LD_PRELOAD=$(ROOT)/libcausal.so ./histogram $(ARGS)
	
