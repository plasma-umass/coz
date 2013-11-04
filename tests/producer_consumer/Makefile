CAUSAL_ROOT = ../..

installed:
	@$(MAKE) -C $(CAUSAL_ROOT) install

producer_consumer: producer_consumer.cpp installed
	clang++ -Xclang -load -Xclang LLVMCausal.dylib -I$(CAUSAL_ROOT)/include -o producer_consumer producer_consumer.cpp -lcausal_rt

test: producer_consumer
	./producer_consumer
