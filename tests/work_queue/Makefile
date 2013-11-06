CAUSAL_ROOT = ../..
CXX = clang++
CXXFLAGS = -g -O0 -std=c++11 -stdlib=libc++ -nostdinc++ -I/usr/src/libcxx/include -L/usr/src/libcxx/lib
CAUSAL_CXXFLAGS = -Xclang -load -Xclang LLVMCausal.dylib $(CXXFLAGS)

default: work_queue
	
clean:
	rm work_queue

installed:
	@$(MAKE) -C $(CAUSAL_ROOT) install

work_queue: work_queue.cpp installed /usr/local/lib/LLVMCausal.dylib /usr/local/lib/libcausal_rt.a
	$(CXX) $(CAUSAL_CXXFLAGS) -I$(CAUSAL_ROOT)/include -o work_queue work_queue.cpp -lcausal_rt

test: work_queue
	./work_queue
