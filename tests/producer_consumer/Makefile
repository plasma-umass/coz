TEST_ROOT = ..

default: producer_consumer producer_consumer_clean
	
clean:
	rm producer_consumer producer_consumer_clean

include $(TEST_ROOT)/tests.mk

producer_consumer: producer_consumer.cpp $(CAUSAL_DEPS)
	$(CAUSAL_COMPILE) -o producer_consumer producer_consumer.cpp
	
producer_consumer_clean: producer_consumer.cpp
	$(CLEAN_COMPILE) -o producer_consumer_clean producer_consumer.cpp

test: producer_consumer
	./producer_consumer
