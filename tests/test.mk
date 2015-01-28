ROOT := $(TEST_ROOT)/..

TARGETS := tester
GTEST_VERSION := gtest-1.7.0
GTEST_DIR := $(TEST_ROOT)/$(GTEST_VERSION)
CXXFLAGS += -I$(GTEST_DIR)/include --std=c++11
LDFLAGS += -L$(TEST_ROOT) -lgtest_main -lgtest -lpthread
PREREQS += $(TEST_ROOT)/libgtest.a

include $(ROOT)/common.mk

test:: tester
	./tester

$(TEST_ROOT)/libgtest.a:
	@cd $(TEST_ROOT); wget https://googletest.googlecode.com/files/$(GTEST_VERSION).zip
	@cd $(TEST_ROOT); rm -rf $(GTEST_VERSION)
	@cd $(TEST_ROOT); unzip $(GTEST_VERSION).zip
	@cd $(TEST_ROOT); rm $(GTEST_VERSION).zip
	@cd $(TEST_ROOT)/$(GTEST_VERSION); ./configure; make
	@cd $(TEST_ROOT); cp $(GTEST_VERSION)/lib/.libs/*.a .
