#if defined(CAUSAL_BUILD)

	#if defined(__cplusplus)
	extern "C" {
	#endif

		#if !defined(CAUSAL_PROGRESS)
			void __causal_progress();
			#define CAUSAL_PROGRESS __causal_progress()
		#endif
	
	#if defined(__cplusplus)
	}
	#endif

#else

	#if !defined(CAUSAL_PROGRESS)
	#define CAUSAL_PROGRESS
	#endif
	
#endif
