# Set the default compilers and flags
CC = clang
CXX = clang++
CFLAGS ?=
CXXFLAGS ?= $(CFLAGS)
CXXLIB = $(CXX) -shared -fPIC
LINKFLAGS ?= -L$(ROOT)/deps/libelfin/dwarf -L$(ROOT)/deps/libelfin/elf

DEBUG_CFLAGS ?= -g
DEBUG_CXXFLAGS ?= $(DEBUG_CFLAGS)
DEBUG_LINKFLAGS ?= -g -L$(ROOT)/debug/lib

RELEASE_CFLAGS ?= -DNDEBUG
RELEASE_CXXFLAGS ?= $(RELEASE_CFLAGS)
RELEASE_LINKFLAGS ?= -L$(ROOT)/release/lib

INSTALL ?=

ifneq ($(INSTALL),)
	INSTALL_DIR = $(ROOT)
else
	INSTALL_DIR = .
endif

# Don't build into subdirectories by default
DIRS ?=

# Don't require any libraries by default
LIBS ?= 

# Set the default include directories
INCLUDE_DIRS += $(ROOT)/include

# Recurse into subdirectories for the 'clean' and 'build' targets
RECURSIVE_TARGETS ?= clean debug release

# Build by default
all:: debug

.PHONY: all debug release clean

# Set the default source and include files with wildcards
SRCS ?= $(wildcard *.c) $(wildcard *.cpp) $(wildcard *.cc) $(wildcard *.C)
OBJS ?= $(addprefix obj/, $(patsubst %.c, %.o, $(patsubst %.cpp, %.o, $(patsubst %.cc, %.o, $(patsubst %.C, %.o, $(SRCS))))))
INCLUDES ?= $(wildcard *.h)\
						$(wildcard *.hpp)\
						$(wildcard *.hh)\
						$(wildcard *.H)\
						$(wildcard include/*.h)\
						$(wildcard include/*.hpp)\
						$(wildcard include/*.hh)\
						$(wildcard include/*.H)\
						$(wildcard $(addsuffix /*.h, $(INCLUDE_DIRS)))\
						$(wildcard $(addsuffix /*.hpp, $(INCLUDE_DIRS)))\
						$(wildcard $(addsuffix /*.hh, $(INCLUDE_DIRS)))\
						$(wildcard $(addsuffix /*.H, $(INCLUDE_DIRS)))

# Clean up all byproducts
clean::
	@rm -rf debug release

INDENT +=" "
export INDENT

# Generate flags to link required libraries and get includes
LIBFLAGS = $(addprefix -l, $(LIBS))
INCFLAGS = -Iinclude $(addprefix -I, $(INCLUDE_DIRS))

# Create separate object lists for debug/release
DEBUG_OBJS = $(addprefix debug/, $(OBJS))
RELEASE_OBJS = $(addprefix release/, $(OBJS))

# Separate target names by type
SHLIB_TARGETS = $(filter %.so, $(TARGETS))
LIB_TARGETS = $(filter %.a, $(TARGETS))
BIN_TARGETS = $(filter-out %.so, $(filter-out %.a, $(TARGETS)))

# Add the local debug path prefix to each target type
DEBUG_SHLIB_TARGETS = $(addprefix debug/lib/, $(SHLIB_TARGETS))
DEBUG_LIB_TARGETS = $(addprefix debug/lib/, $(LIB_TARGETS))
DEBUG_BIN_TARGETS = $(addprefix debug/bin/, $(BIN_TARGETS))
DEBUG_TARGETS = $(DEBUG_SHLIB_TARGETS) $(DEBUG_LIB_TARGETS) $(DEBUG_BIN_TARGETS)

# Add the local release path prefix to each target type
RELEASE_SHLIB_TARGETS = $(addprefix release/lib/, $(SHLIB_TARGETS))
RELEASE_LIB_TARGETS = $(addprefix release/lib/, $(LIB_TARGETS))
RELEASE_BIN_TARGETS = $(addprefix release/bin/, $(BIN_TARGETS))
RELEASE_TARGETS = $(RELEASE_SHLIB_TARGETS) $(RELEASE_LIB_TARGETS) $(RELEASE_BIN_TARGETS)

debug::	$(addprefix $(INSTALL_DIR)/, $(DEBUG_TARGETS))

release:: $(addprefix $(INSTALL_DIR)/, $(RELEASE_TARGETS))

ifneq ($(INSTALL),)

# Copy debug files to the root debug directory
$(addprefix $(INSTALL_DIR)/, $(DEBUG_TARGETS)):: $(DEBUG_TARGETS)
	@echo $(INDENT)[make] Copying `basename $@` to `dirname $@`
	@mkdir -p `dirname $@`
	@cp $< $@

# Copy release files to the root release directory
$(addprefix $(INSTALL_DIR)/, $(RELEASE_TARGETS)):: $(RELEASE_TARGETS)
	@echo $(INDENT)[make] Copying `basename $@` to `dirname $@`
	@mkdir -p `dirname $@`
	@cp $< $@

endif

# Compilation rules for debug

debug/obj/%.o:: %.c Makefile $(ROOT)/common.mk $(INCLUDE_DIRS) $(INCLUDES)
	@mkdir -p debug/obj
	@echo $(INDENT)[$(notdir $(firstword $(CC)))] Compiling $< for debug build
	@$(CC) $(CFLAGS) $(DEBUG_CFLAGS) $(INCFLAGS) -c $< -o $@

debug/obj/%.o:: %.cpp Makefile $(ROOT)/common.mk $(INCLUDE_DIRS) $(INCLUDES)
	@mkdir -p debug/obj
	@echo $(INDENT)[$(notdir $(firstword $(CXX)))] Compiling $< for debug build
	@$(CXX) $(CXXFLAGS) $(DEBUG_CXXFLAGS) $(INCFLAGS) -c $< -o $@
	
debug/obj/%.o:: %.cc Makefile $(ROOT)/common.mk $(INCLUDE_DIRS) $(INCLUDES)
	@mkdir -p debug/obj
	@echo $(INDENT)[$(notdir $(firstword $(CXX)))] Compiling $< for debug build
	@$(CXX) $(CXXFLAGS) $(DEBUG_CXXFLAGS) $(INCFLAGS) -c $< -o $@
	
debug/obj/%.o:: %.C Makefile $(ROOT)/common.mk $(INCLUDE_DIRS) $(INCLUDES)
	@mkdir -p debug/obj
	@echo $(INDENT)[$(notdir $(firstword $(CXX)))] Compiling $< for debug build
	@$(CXX) $(CXXFLAGS) $(DEBUG_CXXFLAGS) $(INCFLAGS) -c $< -o $@

# Linking rules for debug

$(DEBUG_SHLIB_TARGETS):: $(DEBUG_OBJS)  $(INCLUDE_DIRS) $(INCLUDES) Makefile $(ROOT)/common.mk
	@echo $(INDENT)[$(notdir $(firstword $(CXXLIB)))] Linking `basename $@` for debug build
	@mkdir -p debug/lib
	@$(CXXLIB) $(CXXFLAGS) $(LINKFLAGS) $(DEBUG_LINKFLAGS) $(INCFLAGS) $(DEBUG_OBJS) -o $@ $(LIBFLAGS)

$(DEBUG_LIB_TARGETS):: $(DEBUG_OBJS) $(INCLUDE_DIRS) $(INCLUDES) Makefile $(ROOT)/common.mk
	@echo $(INDENT)[ar] Linking `basename $@` for debug build
	@mkdir -p debug/lib
	@ar rcs $@ $(DEBUG_OBJS)

$(DEBUG_BIN_TARGETS):: $(DEBUG_OBJS) $(INCLUDE_DIRS) $(INCLUDES) Makefile $(ROOT)/common.mk
	@echo $(INDENT)[$(notdir $(firstword $(CXX)))] Linking `basename $@` for debug build
	@mkdir -p debug/bin
	@$(CXX) $(CXXFLAGS) $(LINKFLAGS) $(DEBUG_LINKFLAGS) $(INCFLAGS) $(DEBUG_OBJS) -o $@ $(LIBFLAGS)

# Compilation rules for release

release/obj/%.o:: %.c Makefile $(ROOT)/common.mk $(INCLUDE_DIRS) $(INCLUDES)
	@mkdir -p release/obj
	@echo $(INDENT)[$(notdir $(firstword $(CC)))] Compiling $< for release build
	@$(CC) $(CFLAGS) $(RELEASE_CFLAGS) $(INCFLAGS) -c $< -o $@

release/obj/%.o:: %.cpp Makefile $(ROOT)/common.mk $(INCLUDE_DIRS) $(INCLUDES)
	@mkdir -p release/obj
	@echo $(INDENT)[$(notdir $(firstword $(CXX)))] Compiling $< for release build
	@$(CXX) $(CXXFLAGS) $(RELEASE_CXXFLAGS) $(INCFLAGS) -c $< -o $@
	
release/obj/%.o:: %.cc Makefile $(ROOT)/common.mk $(INCLUDE_DIRS) $(INCLUDES)
	@mkdir -p release/obj
	@echo $(INDENT)[$(notdir $(firstword $(CXX)))] Compiling $< for release build
	@$(CXX) $(CXXFLAGS) $(RELEASE_CXXFLAGS) $(INCFLAGS) -c $< -o $@
	
release/obj/%.o:: %.C Makefile $(ROOT)/common.mk $(INCLUDE_DIRS) $(INCLUDES)
	@mkdir -p release/obj
	@echo $(INDENT)[$(notdir $(firstword $(CXX)))] Compiling $< for release build
	@$(CXX) $(CXXFLAGS) $(RELEASE_CXXFLAGS) $(INCFLAGS) -c $< -o $@

# Linking rules for release

$(RELEASE_SHLIB_TARGETS):: $(RELEASE_OBJS)  $(INCLUDE_DIRS) $(INCLUDES) Makefile $(ROOT)/common.mk
	@echo $(INDENT)[$(notdir $(firstword $(CXXLIB)))] Linking `basename $@` for release build
	@mkdir -p release/lib
	@$(CXXLIB) $(CXXFLAGS) $(LINKFLAGS) $(RELEASE_LINKFLAGS) $(INCFLAGS) $(RELEASE_OBJS) -o $@ $(LIBFLAGS)

$(RELEASE_LIB_TARGETS):: $(RELEASE_OBJS) $(INCLUDE_DIRS) $(INCLUDES) Makefile $(ROOT)/common.mk
	@echo $(INDENT)[ar] Linking `basename $@` for release build
	@mkdir -p release/lib
	@ar rcs $@ $(RELEASE_OBJS)

$(RELEASE_BIN_TARGETS):: $(RELEASE_OBJS) $(INCLUDE_DIRS) $(INCLUDES) Makefile $(ROOT)/common.mk
	@echo $(INDENT)[$(notdir $(firstword $(CXX)))] Linking `basename $@` for release build
	@mkdir -p release/bin
	@$(CXX) $(CXXFLAGS) $(LINKFLAGS) $(RELEASE_LINKFLAGS) $(INCFLAGS) $(RELEASE_OBJS) -o $@ $(LIBFLAGS)

# Recursive target rules

$(RECURSIVE_TARGETS)::
	@for dir in $(DIRS); do \
	  echo "$(INDENT)[$@] Entering $$dir"; \
	  $(MAKE) -s -C $$dir $@; \
	done

# Dependencies

$(ROOT)/deps/libelfin/dwarf $(ROOT)/deps/libelfin/elf:
	@echo $(INDENT)[git] Checking out libelfin
	@rm -rf $(ROOT)/deps/libelfin
	@mkdir -p $(ROOT)/deps
	@git clone git@github.com:ccurtsinger/libelfin.git $(ROOT)/deps/libelfin
	@cd $(ROOT)/deps/libelfin; CXXFLAGS=-fPIC make

$(ROOT)/deps/cppgoodies/include:
	@echo $(INDENT)[git] Checking out cppgoodies
	@rm -rf $(ROOT)/deps/cppgoodies
	@mkdir -p $(ROOT)/deps
	@git clone git://github.com/ccurtsinger/cppgoodies.git $(ROOT)/deps/cppgoodies
