ROOT = ../..
TARGET = histogram
LIBS = pthread dl
ARGS = histogram_datafiles/large.bmp
TEST_ARGS = histogram_datafiles/small.bmp

include $(ROOT)/common.mk

setup: histogram_datafiles/small.bmp

histogram_datafiles/%:
	wget http://mapreduce.stanford.edu/datafiles/histogram.tar.gz
	tar xvzf histogram.tar.gz
	rm histogram.tar.gz

bench:: debug/bin/histogram setup
	$(ROOT)/release/bin/causal --- debug/bin/histogram $(ARGS)
	
test:: debug/bin/histogram setup
	$(ROOT)/debug/bin/causal --- debug/bin/histogram $(TEST_ARGS)
