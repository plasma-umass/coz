ROOT = ../..
TARGET = matrix_multiply
LIBS = pthread dl
MATRIX_SIZE = 1000
TEST_MATRIX_SIZE = 400

include $(ROOT)/common.mk

bench:: debug/bin/matrix_multiply
	@rm -f matrix_file_A.txt matrix_file_B.txt
	@echo $(INDENT)[bench] Generating matrix input
	@debug/bin/matrix_multiply $(MATRIX_SIZE) $(MATRIX_SIZE) > /dev/null
	$(ROOT)/release/bin/causal --- debug/bin/matrix_multiply $(MATRIX_SIZE) > /dev/null

test:: debug/bin/matrix_multiply
	@rm -f matrix_file_A.txt matrix_file_B.txt
	@echo $(INDENT)[test] Generating matrix input
	@debug/bin/matrix_multiply $(TEST_MATRIX_SIZE) $(TEST_MATRIX_SIZE) > /dev/null
	$(ROOT)/debug/bin/causal --- debug/bin/matrix_multiply $(TEST_MATRIX_SIZE) > /dev/null
