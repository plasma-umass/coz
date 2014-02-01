CAUSAL_ROOT = $(TEST_ROOT)/..
LLVM_ROOT = $(CAUSAL_ROOT)/../..
PWD = $(shell pwd)
TARGET ?= $(notdir $(PWD))
ARGS ?= 
LIBS ?= 

SHLIB_SUFFIX = so
CXXFLAGS =

# Set platform-specific flags
OS = $(shell uname -s)
ifeq ($(OS),Darwin)
SHLIB_SUFFIX = dylib
CXXFLAGS += -stdlib=libc++ -nostdinc++ -I/usr/src/libcxx/include -L/usr/src/libcxx/lib
endif

CAUSAL_CXXFLAGS = -DCAUSAL_BUILD -Xclang -load -Xclang LLVMCausal.$(SHLIB_SUFFIX) $(CXXFLAGS) -lcausal_rt -ldl -lpthread

CLEAN_COMPILE = clang++ $(CXXFLAGS)
CAUSAL_COMPILE = $(LLVM_ROOT)/Release+Asserts/bin/clang++ $(CAUSAL_CXXFLAGS)

CAUSAL_DEPS = /usr/local/lib/LLVMCausal.$(SHLIB_SUFFIX) /usr/local/lib/libcausal_rt.a

BUILD_TARGETS = bin/$(TARGET)-causal bin/$(TARGET)-clean
SOURCES = $(wildcard *.c) $(wildcard *.cpp)
INCLUDES = $(wildcard *.h) $(wildcard *.hpp)

default: $(BUILD_TARGETS)

clean:
	rm -rf $(BUILD_TARGETS) $(addsuffix .dSYM,$(BUILD_TARGETS))

setup:

$(CAUSAL_DEPS): installed

installed:
	@$(MAKE) -C $(CAUSAL_ROOT) install

bin/%-causal: $(SOURCES) $(INCLUDES) $(CAUSAL_DEPS)
	@mkdir -p bin
	$(CAUSAL_COMPILE) -o $@ $(SOURCES) $(addprefix -l,$(LIBS))

bin/%-clean: $(SOURCES) $(INCLUDES)
	@mkdir -p bin
	$(CLEAN_COMPILE) -o $@ $(SOURCES) $(addprefix -l,$(LIBS))

run-clean: bin/$(TARGET)-clean setup
	$< $(ARGS)

run-causal: bin/$(TARGET)-causal setup
	$< $(ARGS)
