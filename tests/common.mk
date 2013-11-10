CAUSAL_ROOT = $(TEST_ROOT)/..
PWD = $(shell pwd)
TARGET ?= $(notdir $(PWD))
ARGS ?= 
LIBS ?= 

CXXFLAGS = -g -std=c++11 -stdlib=libc++ -nostdinc++ \
	-I/usr/src/libcxx/include -L/usr/src/libcxx/lib -I$(CAUSAL_ROOT)/include

CAUSAL_CXXFLAGS = -DCAUSAL_BUILD -Xclang -load -Xclang LLVMCausal.dylib $(CXXFLAGS) -lcausal_rt

CLEAN_COMPILE = clang++ $(CXXFLAGS)
CAUSAL_COMPILE = clang++ $(CAUSAL_CXXFLAGS)

CAUSAL_DEPS = /usr/local/lib/LLVMCausal.dylib /usr/local/lib/libcausal_rt.a $(CAUSAL_ROOT)/include/causal.h

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
