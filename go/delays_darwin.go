//go:build darwin && cgo

/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

package coz

/*
#include "coz_shim.h"
*/
import "C"

// checkDelays makes this thread pay any virtual delay it owes the profiler.
//
// macOS has no per-thread sampling timer, so a worker thread only discovers its
// delay debt when it reaches a progress point. This mirrors the __APPLE__ arm
// of _COZ_CHECK_DELAYS in include/coz.h.
func checkDelays() { C.coz_shim_add_delays() }
