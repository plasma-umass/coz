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

#ifndef STDDEFINES_H_
#define STDDEFINES_H_

#include <assert.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdio.h>

//#define TIMING

/* Debug printf */
#define dprintf(...) fprintf(stdout, __VA_ARGS__)

/* Wrapper to check for errors */
#define CHECK_ERROR(a)                                       \
   if (a)                                                    \
   {                                                         \
      perror("Error at line\n\t" #a "\nSystem Msg");         \
      assert ((a) == 0);                                     \
   }

static inline void *MALLOC(size_t size)
{
   void * temp = malloc(size);
   assert(temp);
   return temp;
}

static inline void *CALLOC(size_t num, size_t size)
{
   void * temp = calloc(num, size);
   assert(temp);
   return temp;
}

static inline void *REALLOC(void *ptr, size_t size)
{
   void * temp = realloc(ptr, size);
   assert(temp);
   return temp;
}

static inline char *GETENV(char *envstr)
{
   char *env = getenv(envstr);
   if (!env) return "0";
   else return env;
}

#define GET_TIME(start, end, duration)                                     \
   duration.tv_sec = (end.tv_sec - start.tv_sec);                         \
   if (end.tv_nsec >= start.tv_nsec) {                                     \
      duration.tv_nsec = (end.tv_nsec - start.tv_nsec);                   \
   }                                                                       \
   else {                                                                  \
      duration.tv_nsec = (1000000000L - (start.tv_nsec - end.tv_nsec));   \
      duration.tv_sec--;                                                   \
   }                                                                       \
   if (duration.tv_nsec >= 1000000000L) {                                  \
      duration.tv_sec++;                                                   \
      duration.tv_nsec -= 1000000000L;                                     \
   }

static inline unsigned int time_diff (
    struct timeval *end, struct timeval *begin)
{
#ifdef TIMING
    uint64_t result;

    result = end->tv_sec - begin->tv_sec;
    result *= 1000000;     /* usec */
    result += end->tv_usec - begin->tv_usec;

    return result;
#else
    return 0;
#endif
}

static inline void get_time (struct timeval *t)
{
#ifdef TIMING
    gettimeofday (t, NULL);
#endif
}

#endif // STDDEFINES_H_
