ROOT = ../..
TARGETS = string_match
LIBS = pthread dl
ARGS = string_match_datafiles/key_file_500MB.txt > /dev/null

include $(ROOT)/common.mk

setup: string_match_datafiles/key_file_50MB.txt

string_match_datafiles/%:
	wget http://mapreduce.stanford.edu/datafiles/string_match.tar.gz
	tar xvzf string_match.tar.gz
	rm string_match.tar.gz

test:: debug/bin/string_match setup
	LD_PRELOAD=$(ROOT)/debug/lib/libcausal.so ./debug/bin/string_match $(ARGS)
