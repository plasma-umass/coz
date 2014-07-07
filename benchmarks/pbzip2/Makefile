ROOT = ../..
TARGETS = pbzip2
LIBS = pthread dl
ARGS = -c < data/input.txt > data/output.txt

all:: debug
	
clean::
	rm -rf bzip2-1.0.6
	
debug/bin/pbzip2:: debug/lib/libbz2.a

debug/lib/libbz2.a: bzip2-1.0.6
	cd bzip2-1.0.6; make CFLAGS=-g
	mkdir -p debug/lib
	cp bzip2-1.0.6/libbz2.a debug/lib/

bzip2-1.0.6:
	wget http://bzip.org/1.0.6/bzip2-1.0.6.tar.gz
	tar xvzf bzip2-1.0.6.tar.gz
	rm bzip2-1.0.6.tar.gz

include $(ROOT)/common.mk

OBJS += lib/libbz2.a

setup: data/input.txt

data/input.txt:
	@$(MAKE) -C ../linear_regression setup
	mkdir -p data
	cp ../linear_regression/linear_regression_datafiles/key_file_500MB.txt data/input.txt

bench:: debug/bin/pbzip2 setup
	causal -i libbz2 --- debug/bin/pbzip2 $(ARGS)
