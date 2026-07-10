/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

// Command toy is the Go port of benchmarks/toy: two goroutines do unequal
// amounts of work, and a progress point marks each completed round.
//
// slowWork does twice the work of fastWork, so it sits on the critical path.
// A correct causal profile predicts a large speedup for the loop inside
// slowWork and almost none for the one inside fastWork.
//
//	go build -o toy ./example
//	coz run --- ./toy
//	coz plot --text
package main

import (
	"fmt"
	"sync"

	coz "github.com/plasma-umass/coz/go"
)

const iterations = 100_000_000

// Accumulate into package-level variables so the compiler cannot delete the
// loops. Each goroutine writes its own, so there is no race.
var slowSink, fastSink uint64

func slowWork() {
	for i := uint64(0); i < iterations; i++ {
		slowSink += i
	}
}

func fastWork() {
	for i := uint64(0); i < iterations/2; i++ {
		fastSink += i
	}
}

func main() {
	fmt.Println("Starting.")

	for round := 0; round < 100; round++ {
		var wg sync.WaitGroup
		wg.Add(2)
		go func() { defer wg.Done(); slowWork() }()
		go func() { defer wg.Done(); fastWork() }()
		wg.Wait()

		// One round of work finished: this is the throughput progress point.
		coz.Progress()

		fmt.Print(".")
	}

	fmt.Printf("\nDone. %d %d\n", slowSink, fastSink)
}
