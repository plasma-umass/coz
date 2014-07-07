ROOT = ..
DIRS = histogram kmeans linear_regression matrix_multiply pbzip2 pca \
			 producer_consumer string_match unbalanced word_count work_queue
RECURSIVE_TARGETS = debug release test

include $(ROOT)/common.mk
DIRS = histogram linear_regression matrix_multiply \
       pbzip2 pca producer_consumer string_match \
       unbalanced word_count work_queue

experiment:
	@for dir in $(DIRS); do \
		echo "$(INDENT)[$@] Entering $$dir"; \
		$(MAKE) -C $$dir $@ DEBUG=$(DEBUG); \
	done
