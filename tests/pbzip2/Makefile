ROOT = ../..
TARGETS = pbzip2
LIBS = bz2 pthread dl
ARGS = -c < data/input.txt > data/output.txt

include $(ROOT)/common.mk

setup: data/input.txt

data/input.txt:
	@$(MAKE) -C ../linear_regression setup
	mkdir -p data
	cp ../linear_regression/linear_regression_datafiles/key_file_500MB.txt data/input.txt

test:: pbzip2 setup
	LD_PRELOAD=$(ROOT)/libcausal.so ./pbzip2 $(ARGS)
