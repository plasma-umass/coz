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
*     * Neither the name of Stanford University nor the names of its 
*       contributors may be used to endorse or promote products derived from 
*       this software without specific prior written permission.
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

#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>

#define CHECK_ERROR(a)                                       \
   if (a)                                                    \
   {                                                         \
      perror("Error at line\n\t" #a "\nSystem Msg");         \
      exit(1);                                               \
   }

void *sort_section(void *args_in) ;

typedef struct {
	void* base;
	size_t num_elems;
	size_t width;
	int (*compar)(const void *, const void *);
} sort_args;

/** sort_section()
 *  USes qsort to sort the respective portions
 */
void *sort_section(void *args_in) 
{
	sort_args* args =(sort_args*)args_in;

	qsort(args->base, args->num_elems, args->width, args->compar);
	free(args);
	return (void *)0;
}

/** sort_pthreads()
 *  Function that creates the threads to sort
 */
void sort_pthreads(void *base, size_t num_elems, size_t width,
       int (*compar)(const void *, const void *))
{
   int req_units, num_threads, num_procs, i;
   pthread_attr_t attr;
   pthread_t * tid;

   CHECK_ERROR((num_procs = sysconf(_SC_NPROCESSORS_ONLN)) <= 0);
   printf("THe number of processors is %d\n\n", num_procs);

   tid = (pthread_t *)malloc(num_procs * sizeof(pthread_t)); 
   pthread_attr_init(&attr);
   pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

   num_threads = num_procs;

   while(num_threads > 0)
   {
	   req_units = num_elems / num_threads;

	   for(i=0; i<num_threads; i++)
	   {
		   sort_args* out = (sort_args*)malloc(sizeof(sort_args));
		   
		   out->base = ((char*)base) + i*req_units*width;
		   out->num_elems = req_units;
		   if(i == (num_threads - 1))
				out->num_elems = num_elems - i*req_units;
		   out->width = width;
		   out->compar = compar;

		   CHECK_ERROR(pthread_create(&tid[i], &attr, sort_section, (void*)out) != 0);

	   }

	   /* Barrier, wait for all threads to finish */
	   for (i = 0; i < num_threads; i++)
	   {
		  int ret_val;
		  CHECK_ERROR(pthread_join(tid[i], (void **)(void*)&ret_val) != 0);
		  CHECK_ERROR(ret_val != 0);
	   }

	   num_threads = num_threads / 2;
   }


   //qsort(base, num_elems, width, compar);

   free(tid);
}
