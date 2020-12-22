ROOT 		 := ..
TARGETS  := libcoz.so
LIBS     := -ldl -lrt -lpthread $(shell pkg-config --libs libelf++ libdwarf++)
CXXFLAGS := -gdwarf-3 --std=c++0x -g -O2 -fPIC -I$(ROOT)/include -I. \
            $(shell pkg-config --cflags libelf++ libdwarf++)

include $(ROOT)/common.mk

check:: libcoz.so
	printf "int main(int argc, char *argv[])\n{\nreturn (0);\n}\n" > x.c
	gcc -g -o x x.c || ( $(RM) x x.c ; exit 1)
	../coz run --- ./x
	if grep -q time= profile.coz; then echo success: coz profiler ran as it should.; fi
	$(RM) -f x.c x profile.coz
