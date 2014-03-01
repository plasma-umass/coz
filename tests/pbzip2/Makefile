ROOT = ../..
TARGETS = pbzip2
LIBS = bz2 pthread
ARGS = -c < data/input.txt > data/output.txt

include $(ROOT)/common.mk

setup: data/input.txt

data/input.txt:
	@$(MAKE) -C ../linear_regression setup
	mkdir -p data
	cp ../linear_regression/linear_regression_datafiles/key_file_500MB.txt data/input.txt
