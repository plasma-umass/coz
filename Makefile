ROOT := .
DIRS := libcoz

include $(ROOT)/common.mk

bench::
	@for dir in benchmarks ; do make -C $$dir $@; done

install::
	$(INSTALL) -D coz $(DESTDIR)$(bindir)/coz
	$(INSTALL) -D include/coz.h $(DESTDIR)$(incdir)/coz.h

distclean::
	@for dir in deps/libelfin ; do \
	if [ -d $$dir ] ; then make -C $$dir distclean || make -C $$dir clean; fi; \
	done
