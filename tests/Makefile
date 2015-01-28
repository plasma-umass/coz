ROOT := ..
TARGETS := tester
GTEST_VERSION := gtest-1.7.0
CXXFLAGS := -I$(GTEST_VERSION)/include
LDFLAGS := -L. -lgtest_main -lgtest -lpthread
PREREQS := libgtest.a

test:: tester
	./tester

libgtest.a:
	@wget https://googletest.googlecode.com/files/$(GTEST_VERSION).zip
	@rm -rf $(GTEST_VERSION)
	@unzip $(GTEST_VERSION).zip
	@rm $(GTEST_VERSION).zip
	@cd $(GTEST_VERSION); ./configure; make
	@cp $(GTEST_VERSION)/lib/.libs/*.a .

include $(ROOT)/common.mk
