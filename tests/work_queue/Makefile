TEST_ROOT = ..

default: work_queue work_queue_clean
	
clean:
	rm work_queue work_queue_clean

include $(TEST_ROOT)/tests.mk

work_queue: work_queue.cpp $(CAUSAL_DEPS)
	$(CAUSAL_COMPILE) -o work_queue work_queue.cpp
	
work_queue_clean: work_queue.cpp
	$(CLEAN_COMPILE) -o work_queue_clean work_queue.cpp

test: work_queue
	./work_queue
