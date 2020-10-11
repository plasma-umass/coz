ROOT := ..
DIRS := histogram kmeans linear_regression matrix_multiply \
        pbzip2 pca producer_consumer string_match sqlite \
				sqlite-modified toy word_count

include $(ROOT)/common.mk

check::
	@$(MAKE) -C kmeans
	@$(RM) -f profile.coz
	../coz run --- kmeans/kmeans
	@grep -q "time=" profile.coz || { echo failure: valid profile.coz not generated; exit 1; }
	@grep -q "throughput-point" profile.coz || { echo failure: throughput-point not found in profile; exit 1; }
	@grep -q -P "samples\tlocation=" profile.coz || { echo failure: samples not found in profile; exit 1; }
	@echo success: benchmark generated valid profile.coz
	@$(RM) -f profile.coz
