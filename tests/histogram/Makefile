TEST_ROOT = ..
LIBS = pthread
ARGS = histogram_datafiles/small.bmp

include $(TEST_ROOT)/common.mk

setup: histogram_datafiles/small.bmp

histogram_datafiles/%:
	wget http://mapreduce.stanford.edu/datafiles/histogram.tar.gz
	tar xvzf histogram.tar.gz
	rm histogram.tar.gz
