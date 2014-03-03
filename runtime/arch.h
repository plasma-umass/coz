#if !defined(CAUSAL_RUNTIME_ARCH_H)
#define CAUSAL_RUNTIME_ARCH_H

#if defined(__APPLE__)
	#define _OSX(x) x
#else
	#define _OSX(x)
#endif

#if defined(__linux__)
	#define _LINUX(x) x
#else
	#define _LINUX(x)
#endif

#if defined(__i386__)
	#define _X86(x) x
	#define _IS_X86 true
#else
	#define _X86(x)
	#define _IS_X86 false
#endif

#if defined(__x86_64__)
	#define _X86_64(x) x
	#define _IS_X86_64 true
#else
	#define _X86_64(x)
	#define _IS_X86_64 false
#endif

#endif
