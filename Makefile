ROOT := .
DIRS := libcoz viewer

include $(ROOT)/common.mk

update-gh-pages:: all
	@echo $(LOG_PREFIX) Pushing profiler viewer to gh-pages branch $(LOG_SUFFIX)
	@git push origin `git subtree split --prefix viewer master 2> /dev/null`:gh-pages

install:: all
	@echo $(LOG_PREFIX) Installing coz to prefix $(prefix) $(LOG_SUFFIX)
	@$(INSTALL) -D coz $(DESTDIR)$(bindir)/coz
	@$(INSTALL) -D libcoz/libcoz.so $(DESTDIR)$(pkglibdir)/libcoz.so
	@$(INSTALL) -D include/coz.h $(DESTDIR)$(incdir)/coz.h
	@mkdir -p $(DESTDIR)$(man1dir)
	@$(RST2MAN) docs/coz.rst $(DESTDIR)$(man1dir)/coz.1

bench::
	@$(MAKE) -C benchmarks bench
