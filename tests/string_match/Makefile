TEST_ROOT = ..
LIBS = pthread
ARGS = string_match_datafiles/key_file_50MB.txt

include $(TEST_ROOT)/common.mk

setup: string_match_datafiles/key_file_50MB.txt

string_match_datafiles/%:
	wget http://mapreduce.stanford.edu/datafiles/string_match.tar.gz
	tar xvzf string_match.tar.gz
	rm string_match.tar.gz
