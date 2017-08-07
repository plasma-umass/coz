ROOT := .
DIRS := libcoz viewer

include $(ROOT)/common.mk

bench::
	@for dir in benchmarks ; do make -C $$dir $@; done

check::
	make -C libcoz $@

doc::

install::
	$(INSTALL) -D coz $(DESTDIR)$(bindir)/coz
	$(INSTALL) -D include/coz.h $(DESTDIR)$(incdir)/coz.h
	$(INSTALL) -D doc/coz.1 $(DESTDIR)$(mandir)/coz.1

distclean::
	@for dir in deps/libelfin ; do \
	if [ -d $$dir ] ; then make -C $$dir distclean || make -C $$dir clean; fi; \
	done
