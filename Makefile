ROOT := .
DIRS := libcoz benchmarks

include $(ROOT)/common.mk

install::
	$(INSTALL) -D coz $(DESTDIR)$(bindir)/coz
	$(INSTALL) -D include/coz.h $(DESTDIR)$(incdir)/coz.h
