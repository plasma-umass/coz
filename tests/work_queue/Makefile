CAUSAL_ROOT = ../..
CXX = clang++
CXXFLAGS = -std=c++11 -stdlib=libc++ -nostdinc++ -I/usr/src/libcxx/include -L/usr/src/libcxx/lib
CAUSAL_CXXFLAGS = -Xclang -load -Xclang LLVMCausal.dylib $(CXXFLAGS)

default: work_queue work_queue_clean
	
clean:
	rm work_queue work_queue_clean

installed:
	@$(MAKE) -C $(CAUSAL_ROOT) install

work_queue: work_queue.cpp installed /usr/local/lib/LLVMCausal.dylib /usr/local/lib/libcausal_rt.a
	$(CXX) $(CAUSAL_CXXFLAGS) -I$(CAUSAL_ROOT)/include -o work_queue work_queue.cpp -lcausal_rt

work_queue_clean: work_queue.cpp installed /usr/local/lib/libcausal_rt.a
	$(CXX) $(CXXFLAGS) -I$(CAUSAL_ROOT)/include -o work_queue_clean work_queue.cpp -lcausal_rt

test: work_queue
	./work_queue
