//go:build !cgo

/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

// Pure-Go no-op implementation for CGO_ENABLED=0 builds.
//
// Reaching libcoz requires dlsym, which requires cgo, and coz profiles a binary
// by injecting libcoz into it at load time -- neither of which applies to a
// static CGO_ENABLED=0 binary. Rather than break the build of any package that
// imports coz, this stub keeps the API present and inert, so a project can leave
// its progress points in place and simply build with cgo enabled when it wants
// to profile.
package coz

// Available always reports false without cgo.
func Available() bool { return false }

// A Counter is an inert progress-point handle.
type Counter struct{}

// NewThroughput returns an inert throughput counter.
func NewThroughput(string) *Counter { return &Counter{} }

// NewBegin returns an inert latency-begin counter.
func NewBegin(string) *Counter { return &Counter{} }

// NewEnd returns an inert latency-end counter.
func NewEnd(string) *Counter { return &Counter{} }

// Increment does nothing.
func (c *Counter) Increment() {}

// Progress does nothing.
func Progress() {}

// ProgressNamed does nothing.
func ProgressNamed(string) {}

// Begin does nothing.
func Begin(string) {}

// End does nothing.
func End(string) {}

// Scope does nothing and returns a function that does nothing.
func Scope(string) func() { return func() {} }
