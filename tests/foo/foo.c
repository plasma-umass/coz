#include <stdio.h>
#include <stdlib.h>

void foo() {
	printf("Hello foo()\n");
}

void bar() {
	printf("Hello bar()\n");
}

int main(int argc, char** argv) {
	printf("Hello main()\n");
	foo();
	bar();
	return 0;
}
