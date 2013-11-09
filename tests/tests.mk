CAUSAL_ROOT = $(TEST_ROOT)/..

CXXFLAGS = -g -std=c++11 -stdlib=libc++ -nostdinc++ \
	-I/usr/src/libcxx/include -L/usr/src/libcxx/lib -I$(CAUSAL_ROOT)/include

CAUSAL_CXXFLAGS = -DCAUSAL_BUILD -Xclang -load -Xclang LLVMCausal.dylib $(CXXFLAGS) -lcausal_rt

CLEAN_COMPILE = clang++ $(CXXFLAGS)
CAUSAL_COMPILE = clang++ $(CAUSAL_CXXFLAGS)

CAUSAL_DEPS = /usr/local/lib/LLVMCausal.dylib /usr/local/lib/libcausal_rt.a $(CAUSAL_ROOT)/include/causal.h

$(CAUSAL_DEPS): installed

installed:
	@$(MAKE) -C $(CAUSAL_ROOT) install
