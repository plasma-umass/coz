ROOT = ../..
TARGETS = matrix_multiply
LIBS = pthread dl
ARGS = 1024	> /dev/null

include $(ROOT)/common.mk

setup: matrix_file_A.txt matrix_file_B.txt

matrix_file_A.txt matrix_file_B.txt: matrix_multiply
	./matrix_multiply 1024 1024 > /dev/null

test:: matrix_multiply setup
	LD_PRELOAD=$(ROOT)/libcausal.so ./matrix_multiply $(ARGS)
