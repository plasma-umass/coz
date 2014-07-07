ROOT = ../..
TARGETS = matrix_multiply
LIBS = pthread dl
MATRIX_SIZE = 800
ARGS = $(MATRIX_SIZE)	> /dev/null

include $(ROOT)/common.mk

setup: matrix_file_A.txt matrix_file_B.txt

matrix_file_A.txt matrix_file_B.txt: debug/bin/matrix_multiply
	./debug/bin/matrix_multiply $(MATRIX_SIZE) $(MATRIX_SIZE) > /dev/null

test:: debug/bin/matrix_multiply setup
	LD_PRELOAD=$(ROOT)/debug/lib/libcausal.so ./debug/bin/matrix_multiply $(ARGS)
