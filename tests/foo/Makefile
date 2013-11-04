CAUSAL_ROOT = ../..

installed:
	@$(MAKE) -C $(CAUSAL_ROOT) install

foo: foo.c installed
	clang -Xclang -load -Xclang LLVMCausal.dylib -I$(CAUSAL_ROOT)/include -o foo foo.c -lcausal_rt -lc++

test: foo
	./foo
