ROOT    := ../..
TARGETS := matrix_multiply
LIBS    := pthread
CFLAGS  := -g -O2 -Wno-format

include $(ROOT)/benchmark.mk

BENCH_LARGE_ARGS := 1000 > /dev/null
BENCH_SMALL_ARGS := 400 > /dev/null

large_inputs: .input_large

small_inputs: .input_small

.input_large: matrix_multiply
	@echo $(LOG_PREFIX) Generating bench input $(LOG_SUFFIX)
	@./matrix_multiply 1000 1000 > /dev/null
	@rm -f .input_small
	@touch .input_large

.input_small: matrix_multiply
	@echo $(LOG_PREFIX) Generating test input $(LOG_SUFFIX)
	@./matrix_multiply 400 400 > /dev/null
	@rm -f .input_large
	@touch .input_small
