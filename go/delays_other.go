//go:build !darwin && cgo

/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

package coz

// checkDelays is a no-op off macOS.
//
// On Linux the SIGPROF handler already applies delays via process_samples(), so
// calling _coz_add_delays() again at every progress point would apply them
// twice and collapse throughput under concurrency. This mirrors the non-Apple
// arm of _COZ_CHECK_DELAYS in include/coz.h.
func checkDelays() {}
