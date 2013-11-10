TEST_ROOT = ..
ARGS = string_match_datafiles/key_file_500MB.txt > /dev/null

include $(TEST_ROOT)/common.mk

setup: string_match_datafiles/key_file_500MB.txt

string_match_datafiles/%:
	wget http://mapreduce.stanford.edu/datafiles/string_match.tar.gz
	tar xvzf string_match.tar.gz
	rm string_match.tar.gz
