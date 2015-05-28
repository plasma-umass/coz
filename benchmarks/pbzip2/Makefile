ROOT 	   := ../..
TARGETS  := pbzip2
LIBS     := bz2 pthread dl
CXXFLAGS := -g -O2 -Wno-format -I$(ROOT)/include -Ibzip2-1.0.6
LDFLAGS  := -Lbzip2-1.0.6

BENCHMARK := 1

include $(ROOT)/common.mk

# pbzip2 depends on the libbz2 library
pbzip2: bzip2-1.0.6/libbz2.a

# Download and build libbz2
bzip2-1.0.6/libbz2.a:
	@echo $(LOG_PREFIX) Downloading libbz2 $(LOG_SUFFIX)
	@wget -c http://bzip.org/1.0.6/bzip2-1.0.6.tar.gz
	@echo $(LOG_PREFIX) Unpacking libbz2 $(LOG_SUFFIX)
	@tar xzf bzip2-1.0.6.tar.gz
	@echo $(LOG_PREFIX) Building libbz2 $(LOG_SUFFIX)
	@cd bzip2-1.0.6; $(MAKE) CFLAGS=-g > /dev/null
	@rm bzip2-1.0.6.tar.gz

BENCH_ARGS := -c < data/key_file_500MB.txt > data/key_file_500MB_compressed.bz2
TEST_ARGS  := -c < data/key_file_50MB.txt > data/key_file_50MB_compressed.bz2

bench_inputs: data/key_file_500MB.txt

test_inputs: data/key_file_50MB.txt

data/%:
	@echo $(LOG_PREFIX) Downloading pbzip2 inputs $(LOG_SUFFIX)
	@wget -c http://csl.stanford.edu/~christos/data/linear_regression.tar.gz
	@echo $(LOG_PREFIX) Unpacking pbzip2 inputs $(LOG_SUFFIX)
	@tar xzf linear_regression.tar.gz
	@rm linear_regression.tar.gz
	@mv linear_regression_datafiles data
