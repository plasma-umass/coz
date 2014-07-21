/* Copyright (c) 2007-2009, Stanford University
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*     * Neither the name of Stanford University nor the
*       names of its contributors may be used to endorse or promote products
*       derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY STANFORD UNIVERSITY ``AS IS'' AND ANY
* EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL STANFORD UNIVERSITY BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/ 

#ifndef MAP_REDUCE_H_
#define MAP_REDUCE_H_

#include <sys/types.h>
#include <stdbool.h>

/* Standard data types for the function arguments and results */
 
/* Argument to map function. This is specified by the splitter function.
 * length - number of elements of data. The default splitter function gives 
            length in terms of the # of elements of unit_size bytes.
 * data - data to process of a user defined type
 */
typedef struct
{
   intptr_t length;
   void *data;
} map_args_t;

/* Single element of result
 * key - pointer to the key
 * val - pointer to the value
 */
typedef struct
{
   void *key;
   void *val;
} keyval_t;

/* List of results
 * length - number of key value pairs
 * data - array of key value pairs
 */
typedef struct
{
   int length;
   keyval_t *data;
} final_data_t;

/* Scheduler function pointer type definitions */

/* Map function takes in map_args_t, as supplied by the splitter
 * and emit_intermediate() should be called on any key value pairs 
 * in the intermediate result set.
 */
typedef void (*map_t)(map_args_t *);

struct iterator_t;
typedef struct iterator_t iterator_t;
int iter_next (iterator_t *itr, void **);
int iter_size (iterator_t *itr);

/* Reduce function takes in a key pointer, a list of value pointers, and a 
 * length of the list. emit() should be called on any key value pairs 
 * in the result set.
 */
typedef void (*reduce_t)(void *, iterator_t *itr);

/* Combiner function takes in an iterator for a particular key,
 * and returns a reduced value. The operation should be identical to the 
 * reduce function, except that this function returns the reduced value 
 * directly. */
typedef void *(*combiner_t)(iterator_t *itr);

/* Splitter function takes in a pointer to the input data, an interger of
 * the number of bytes requested, and an uninitialized pointer to a 
 * map_args_t pointer. The result is stored in map_args_t. The splitter
 * should return 1 if the result is valid or 0 if there is no more data.
 */
typedef int (*splitter_t)(void *, int, map_args_t *);

/* Locator function takes in a pointer to map_args_t, and returns
 * the memory address where this map task would be heavily accessing.
 * The runtime would then try to dispatch the task to a thread that
 * is nearby the physical memory that backs the address. */
typedef void* (*locator_t)(map_args_t *);

/* Partition function takes in the number of reduce tasks, a pointer to
 * a key, and the lendth of the key in bytes. It assigns a key to a reduce task.
 * The value returned is the # of the reduce task where the key will be processed. 
 * This value should be the same for all keys that are equal.
 */
typedef int (*partition_t)(int, void *, int);

/* key_cmp(key1, key2) returns:
 *   0 if key1 == key2
 *   + if key1 > key2
 *   - if key1 < key2 
 */
typedef int (*key_cmp_t)(const void *, const void*);

/* The arguments to operate the runtime. */
typedef struct
{
    void * task_data;           /* The data to run MapReduce on.
                                 * If splitter is NULL, this should be an array. */
    off_t data_size;            /* Total # of bytes of data */
    int unit_size;              /* # of bytes for one element 
                                 * (if necessary, on average) */

    map_t map;                  /* Map function pointer, must be user defined */
    reduce_t reduce;            /* If NULL, identity reduce function is used, 
                                 * which emits a keyval pair for each val. */
    combiner_t combiner;        /* If NULL, no combiner would be called. */                             
    splitter_t splitter;        /* If NULL, the array splitter is used.*/
    locator_t locator;          /* If NULL, no locality based optimization is
                                   performed. */
    key_cmp_t key_cmp;          /* Key comparison function. 
                                   Must be user defined.*/

    final_data_t *result;       /* Pointer to output data. 
                                 * Must be allocated by user */

    /*** Optional arguments must be zero if not used ***/
    partition_t partition;      /* Default partition function is a 
                                 * hash function */

    /* Creates one emit queue for each reduce task,
    * instead of per reduce thread. This improves
    * time to emit if data is emitted in order,
    * but can increase merge time. */
    bool use_one_queue_per_task;    

    int L1_cache_size;     /* Size of L1 cache in bytes */
    int num_map_threads;   /* # of threads to run map tasks on.
                                 * Default is one per processor */
    int num_reduce_threads;     /* # of threads to run reduce tasks on.
    * Default is one per processor */
    int num_merge_threads;      /* # of threads to run merge tasks on.
    * Default is one per processor */
    int num_procs;              /* Maximum number of processors to use. */

    int proc_offset;            /* number of procs to skip for thread binding */
                                /* (useful if you have multiple MR's running
                                 *  and you don't want them binding to the same
                                 *  hardware thread). */

    float key_match_factor;     /* Magic number that describes the ratio of 
    * the input data size to the output data size.
    * This is used as a hint. */
} map_reduce_args_t;

/* Runtime defined functions. */

/* MapReduce initialization function. Called once per process. */
int map_reduce_init ();

/* MapReduce finalization function. Called once per process. */
int map_reduce_finalize ();

/* The main MapReduce engine. This is the function called by the application.
 * It is responsible for creating and scheduling all map and reduce tasks, and
 * also organizes and maintains the data which is passed from application to 
 * map tasks, map tasks to reduce tasks, and reduce tasks back to the
 * application. Results are stored in args->result. A return value less than zero
 * represents an error. This function is not thread safe. 
 */   
int map_reduce (map_reduce_args_t * args);

/* This should be called from the map function. It stores a key with key_size
 * bytes and a value in the intermediate queues for processing by the reduce 
 * task. The runtime will call partiton function to assign the key to a 
 * reduce task.
 */
void emit_intermediate(void *key, void *val, int key_size);

/* This should be called from the reduce function. It stores a key and a value 
 * in the reduce queue. This will be in the final result array.
 */
void emit(void *key, void *val);

/* This is the built in partition function which is a hash.  It is global so 
 * the user defined partition function can call it.
 */
int default_partition(int reduce_tasks, void* key, int key_size);

#endif // MAP_REDUCE_H_
