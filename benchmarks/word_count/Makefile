ROOT = ../..
TARGET = word_count
LIBS = pthread dl
ARGS = word_count_datafiles/word_100MB.txt
TEST_ARGS = word_count_datafiles/word_10MB.txt

include $(ROOT)/common.mk

setup: word_count_datafiles/word_100MB.txt

word_count_datafiles/%:
	wget http://mapreduce.stanford.edu/datafiles/word_count.tar.gz
	tar xvzf word_count.tar.gz
	rm word_count.tar.gz

bench:: debug/bin/word_count setup
	$(ROOT)/release/bin/causal --- debug/bin/word_count $(ARGS)

test:: debug/bin/word_count setup
	$(ROOT)/debug/bin/causal --- debug/bin/word_count $(TEST_ARGS)
