/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#ifndef COZ_MAC_INTERPOSE_H
#define COZ_MAC_INTERPOSE_H

#ifdef __APPLE__

/**
 * macOS DYLD interposition support.
 *
 * On macOS, function interposition is done via DYLD_INSERT_LIBRARIES.
 * This header provides macros for creating interposition tuples that
 * tell the dynamic linker which functions to replace.
 *
 * The interposition mechanism uses the __DATA,__interpose section.
 */

/**
 * Structure used by dyld for interposition.
 * When placed in the __DATA,__interpose section, dyld will replace
 * calls to 'original' with calls to 'replacement'.
 */
typedef struct {
  const void* replacement;
  const void* original;
} interpose_tuple_t;

/**
 * Macro to create an interposition entry.
 * Usage: INTERPOSE(my_function, original_function)
 *
 * This creates a static entry in the __DATA,__interpose section that
 * tells dyld to replace calls to 'original_function' with 'my_function'.
 */
#define INTERPOSE(replacement, original) \
  __attribute__((used, section("__DATA,__interpose"))) \
  static const interpose_tuple_t interpose_##original = { \
    (const void*)(unsigned long)&replacement, \
    (const void*)(unsigned long)&original \
  }

/**
 * Alternative macro using dyld_interposing_tuple from mach-o/dyld-interposing.h
 * if available. This is the official Apple mechanism.
 */
#ifdef DYLD_INTERPOSE
#undef INTERPOSE
#define INTERPOSE(replacement, original) DYLD_INTERPOSE(replacement, original)
#endif

#endif // __APPLE__

#endif // COZ_MAC_INTERPOSE_H
