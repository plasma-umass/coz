/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#ifdef __APPLE__

#include "mac_interpose.h"

/**
 * macOS interposition support implementation.
 *
 * On macOS, coz uses DYLD_INSERT_LIBRARIES to inject the profiler library.
 * Function interposition is achieved through:
 *
 * 1. Symbol interposition: Functions defined in libcoz.cpp with the same
 *    names as system functions (e.g., pthread_create, pthread_mutex_lock)
 *    will override the system versions when the library is loaded.
 *
 * 2. DYLD interposition tuples: For more reliable interposition, the
 *    __DATA,__interpose section can be used to explicitly specify
 *    which functions to replace.
 *
 * Currently, coz relies on symbol interposition (method 1) which works
 * well with DYLD_INSERT_LIBRARIES. The interposition tuples (method 2)
 * are available via the INTERPOSE macro in mac_interpose.h if needed
 * for specific functions that don't interpose correctly.
 *
 * Note: On macOS, some functions may require explicit interposition
 * tuples due to two-level namespace lookup. If interposition issues
 * are encountered, add INTERPOSE entries here.
 */

// Currently no explicit interposition tuples are needed.
// The symbol-based interposition in libcoz.cpp handles the required
// pthread and signal functions.

#endif // __APPLE__
