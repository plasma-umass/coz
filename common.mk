DESTDIR   ?=
prefix    ?= /usr
bindir    := $(prefix)/bin
pkglibdir := $(prefix)/lib/coz-profiler
incdir    := $(prefix)/include
mandir    := $(prefix)/share/man
man1dir   := $(mandir)/man1

INSTALL = install
RST2MAN = rst2man

# Build with clang by default
CC  ?= clang
CXX ?= clang++

# Set coz and include path for coz
ifeq ($(USE_SYSTEM_COZ),1)
COZ := $(shell which coz)
else
COZ := $(ROOT)/coz
endif

# Default flags
CFLAGS   ?= -g -O2
CXXFLAGS ?= $(CFLAGS)

# Default source and object files
SRCS ?= $(wildcard *.cpp) $(wildcard *.c)
OBJS ?= $(addprefix obj/,$(patsubst %.cpp,%.o,$(patsubst %.c,%.o,$(SRCS))))

# Prevent errors if files named all, clean, distclean, bench, or test exist
.PHONY: all clean distclean bench bench_small bench_large test

# Targets to build recursively into $(DIRS)
RECURSIVE_TARGETS  ?= all clean bench bench_large bench_small test install check

# Targets separated by type
SHARED_LIB_TARGETS := $(filter %.so, $(TARGETS))
STATIC_LIB_TARGETS := $(filter %.a, $(TARGETS))
OTHER_TARGETS      := $(filter-out %.so, $(filter-out %.a, $(TARGETS)))

# If not set, the build path is just the current directory name
MAKEPATH ?= $(shell basename $(shell pwd))

# Log the build path in gray, following by a log message in bold green
LOG_PREFIX := "$(shell tput setaf 7)[$(MAKEPATH)]$(shell tput sgr0)$(shell tput setaf 2)"
LOG_SUFFIX := "$(shell tput sgr0)"

# Build in parallel
MAKEFLAGS += -j

# Build all targets by default, unless this is a benchmark
all:: $(TARGETS)

# Clean up after a build
clean::
	@for t in $(TARGETS); do \
	echo $(LOG_PREFIX) Cleaning $$t $(LOG_SUFFIX); \
	done
	@rm -rf $(TARGETS) obj

# Bring source back to pristine state
distclean:: clean
	@$(MAKE) -C benchmarks clean

# Compile a C++ source file (and generate its dependency rules)
obj/%.o: %.cpp $(PREREQS)
	@echo $(LOG_PREFIX) Compiling $< $(LOG_SUFFIX)
	@mkdir -p obj
	@$(CXX) $(CXXFLAGS) -MMD -MP -o $@ -c $<

# Compile a C source file (and generate its dependency rules)
obj/%.o: %.c $(PREREQS)
	@echo $(LOG_PREFIX) Compiling $< $(LOG_SUFFIX)
	@mkdir -p obj
	@$(CC) $(CFLAGS) -MMD -MP -o $@ -c $<

# Link a shared library
$(SHARED_LIB_TARGETS): $(OBJS)
	@echo $(LOG_PREFIX) Linking $@ $(LOG_SUFFIX)
	@$(CXX) -shared $(LDFLAGS) -o $@ $^ $(LIBS)

$(STATIC_LIB_TARGETS): $(OBJS)
	@echo $(LOG_PREFIX) Linking $@ $(LOG_SUFFIX)
	@ar rs $@ $^

# Link binary targets
$(OTHER_TARGETS): $(OBJS)
	@echo $(LOG_PREFIX) Linking $@ $(LOG_SUFFIX)
	@$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

# Include dependency rules for all objects
-include $(OBJS:.o=.d)

# Build any recursive targets in subdirectories
$(RECURSIVE_TARGETS)::
	@for dir in $(DIRS); do \
	$(MAKE) -C $$dir --no-print-directory $@ MAKEPATH="$(MAKEPATH)/$$dir" || exit 1; \
	done
