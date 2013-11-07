CAUSAL_ROOT = ../..
CXX = clang++
CXXFLAGS = -g -O0 -std=c++11 -stdlib=libc++ -nostdinc++ -I/usr/src/libcxx/include -L/usr/src/libcxx/lib -I$(CAUSAL_ROOT)/include
CAUSAL_CXXFLAGS = -DCAUSAL_BUILD -Xclang -load -Xclang LLVMCausal.dylib $(CXXFLAGS)

default: work_queue work_queue_clean
	
clean:
	rm work_queue

installed:
	@$(MAKE) -C $(CAUSAL_ROOT) install

work_queue: work_queue.cpp installed /usr/local/lib/LLVMCausal.dylib /usr/local/lib/libcausal_rt.a
	$(CXX) $(CAUSAL_CXXFLAGS) -o work_queue work_queue.cpp -lcausal_rt
	
work_queue_clean: work_queue.cpp
	$(CXX) $(CXXFLAGS) -o work_queue_clean work_queue.cpp

test: work_queue
	./work_queue
