ROOT 		 := ..
TARGETS  := libcoz.so
LIBS     := dl rt pthread
CXXFLAGS := --std=c++11 -g -O2 -fPIC -I$(ROOT)/include
#LDFLAGS  := -L$(ROOT)/deps/libelfin/elf -L$(ROOT)/deps/libelfin/dwarf -Wl,--whole-archive -lelf++ -ldwarf++ -Wl,--no-whole-archive
PREREQS  := $(ROOT)/deps/ccutil $(ROOT)/deps/libelfin

include $(ROOT)/common.mk

check:: libcoz.so
	printf "int main(int argc, char *argv[])\n{\nreturn (0);\n}\n" > x.c
	gcc -Wl,-rpath,`pwd` -o x x.c libcoz.so -ldl || ( $(RM) x x.c ; exit 1)
	./x
	if grep -q time= profile.coz; then echo success: coz profiler ran as it should.; fi
	$(RM) -f x.c x profile.coz

install::
	$(INSTALL) -D $(ROOT)/libcoz/libcoz.so $(DESTDIR)$(pkglibdir)/libcoz.so
