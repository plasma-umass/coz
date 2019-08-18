ROOT   := ..

include $(ROOT)/common.mk

all:: js/ui.js

js/ui.js: $(wildcard ts/*.ts) tsconfig.json
	@echo $(LOG_PREFIX) Building profile viewer $(LOG_SUFFIX)
	@npm install > /dev/null
