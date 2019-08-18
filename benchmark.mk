include $(ROOT)/common.mk

RECURSIVE_TARGETS += bench bench_large bench_small

ifeq ($(USE_SYSTEM_COZ),)
CFLAGS   += -I$(ROOT)/include
CXXFLAGS += -I$(ROOT)/include
endif

# Set up build targets for benchmarking
large_inputs:

small_inputs:

bench:: bench_large

bench_large:: $(OTHER_TARGETS) large_inputs
	@echo $(LOG_PREFIX) Running benchmark on large input $(LOG_SUFFIX)
	$(COZ) run $(COZ_ARGS) --- ./$< $(BENCH_LARGE_ARGS)

bench_small:: $(OTHER_TARGETS) small_inputs
	@echo $(LOG_PREFIX) Running benchmark on small input $(LOG_SUFFIX)
	$(COZ) run $(COZ_ARGS) --- ./$< $(BENCH_SMALL_ARGS)
