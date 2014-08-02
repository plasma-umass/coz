ROOT = ../..
TARGET = coz-process
INSTALL = 1

debug: $(ROOT)/debug/bin/coz-process

release: $(ROOT)/release/bin/coz-process

$(ROOT)/debug/bin/coz-process $(ROOT)/release/bin/coz-process:: coz-process
	@cp coz-process $@
