/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#ifndef COZ_ELF_COMPAT_H
#define COZ_ELF_COMPAT_H

/**
 * ELF compatibility definitions for macOS.
 *
 * On Linux, <elf.h> provides ELF format definitions. On macOS, these
 * types don't exist natively, so we provide minimal compatibility
 * definitions for the types used by coz.
 */

#ifdef __APPLE__

#include <cstdint>

/**
 * ELF Note header structure.
 * Used to parse NOTE sections in ELF files.
 */
typedef struct {
  uint32_t n_namesz;  // Length of the note's name
  uint32_t n_descsz;  // Length of the note's descriptor
  uint32_t n_type;    // Type of the note
} Elf64_Nhdr;

/**
 * Note type for GNU build ID.
 * The build ID is a unique identifier for a particular build of a binary.
 */
#ifndef NT_GNU_BUILD_ID
#define NT_GNU_BUILD_ID 3
#endif

#endif // __APPLE__

#endif // COZ_ELF_COMPAT_H
