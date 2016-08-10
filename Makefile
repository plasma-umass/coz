ROOT := .
DIRS := libcoz benchmarks

include $(ROOT)/common.mk

install::
	$(INSTALL) -D coz $(DESTDIR)$(bindir)/coz
	$(INSTALL) -D include/coz.h $(DESTDIR)$(incdir)/coz.h

distclean::
	@for dir in deps/libelfin ; do \
	make -C $$dir distclean || make -C $$dir clean; \
	done
