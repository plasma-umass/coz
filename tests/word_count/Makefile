ROOT = ../..
TARGETS = word_count
LIBS = pthread dl
ARGS = word_count_datafiles/word_100MB.txt

include $(ROOT)/common.mk

setup: word_count_datafiles/word_100MB.txt

word_count_datafiles/%:
	wget http://mapreduce.stanford.edu/datafiles/word_count.tar.gz
	tar xvzf word_count.tar.gz
	rm word_count.tar.gz

test:: word_count setup
	LD_PRELOAD=$(ROOT)/libcausal.so ./word_count $(ARGS)
