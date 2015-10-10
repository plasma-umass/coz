ROOT    := ../..
TARGETS := matrix_multiply
LIBS    := pthread dl
CFLAGS  := -g -O2 -Wno-format

ifeq ($(USE_SYSTEM_COZ),)
CFLAGS += -I$(ROOT)/include
endif

BENCHMARK := 1

include $(ROOT)/common.mk

BENCH_ARGS := 1000 > /dev/null
TEST_ARGS  := 400 > /dev/null

bench_inputs: .input_bench

test_inputs: .input_test

.input_bench:
	@echo $(LOG_PREFIX) Generating bench input $(LOG_SUFFIX)
	@./matrix_multiply 1000 1000 > /dev/null
	@rm -f .input_test
	@touch .input_bench

.input_test:
	@echo $(LOG_PREFIX) Generating test input $(LOG_SUFFIX)
	@./matrix_multiply 400 400 > /dev/null
	@rm -f .input_bench
	@touch .input_test
