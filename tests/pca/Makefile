<<<<<<< HEAD
ROOT = ../..
TARGETS = pca
LIBS = pthread dl
ARGS = > /dev/null
=======
TEST_ROOT = ..
LIBS = pthread
ARGS =
>>>>>>> 4d5628d162b7cdfa13645b696cfc187126b21929

include $(ROOT)/common.mk

test:: pca
	LD_PRELOAD=$(ROOT)/libcausal.so ./pca $(ARGS)
