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
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <pthread.h>

#include "stddefines.h"
#include "sort-pthread.h"

#include "coz.h"

#define DEFAULT_DISP_NUM 10
#define START_ARRAY_SIZE 2000

typedef struct {
	char* word;
	int count;
} wc_count_t;

typedef struct {
   long fpos;
   long flen;
   char *fdata;
   int unit_size;
} wc_data_t;

typedef struct
{
   int length;
   void *data;
   int t_num;
} t_args_t;

enum {
   IN_WORD,
   NOT_IN_WORD
};

typedef struct
{
   int length1;
   int length2;
   int length_out_pos;
   wc_count_t *data1;
   wc_count_t *data2;
   wc_count_t *out;
} merge_data_t;

wc_count_t** words;
int* use_len;
int* length;


void *wordcount_map(void *args_in);
void wordcount_reduce(char* word, int len) ;
void *merge_sections(void *args_in);

/** mystrcmp()
 *  Comparison function to compare 2 words
 */
inline int mystrcmp(const void *s1, const void *s2)
{
   return strcmp((const char *)s1, (const char *) s2);
}

/** wordcount_cmp()
 *  Comparison function to compare 2 words
 */
int wordcount_cmp(const void *v1, const void *v2)
{
   wc_count_t* w1 = (wc_count_t*)v1;
   wc_count_t* w2 = (wc_count_t*)v2;

   int i1 = w1->count;
   int i2 = w2->count;

   if (i1 < i2) return 1;
   else if (i1 > i2) return -1;
   else return 0;
}

/** wordcount_splitter()
 *  Memory map the file and divide file on a word border i.e. a space.
 *	Assign each portion of the file to a thread
 */
void wordcount_splitter(void *data_in)
{
   pthread_attr_t attr;
   pthread_t * tid;
   int i,num_procs;

   CHECK_ERROR((num_procs = sysconf(_SC_NPROCESSORS_ONLN)) <= 0);
   dprintf("THe number of processors is %d\n\n", num_procs);

   wc_data_t * data = (wc_data_t *)data_in; 
   tid = (pthread_t *)MALLOC(num_procs * sizeof(pthread_t));  

   /* Thread must be scheduled systemwide */
   pthread_attr_init(&attr);
   pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

   words = (wc_count_t**)malloc(num_procs*sizeof(wc_count_t*));
   length = (int*)malloc(num_procs*sizeof(int));
   use_len = (int*)malloc(num_procs*sizeof(int));

   int req_bytes = data->flen / num_procs;

   for(i=0; i<num_procs; i++)
   {
      words[i] = (wc_count_t*)malloc(START_ARRAY_SIZE*sizeof(wc_count_t));
      length[i] = START_ARRAY_SIZE;
      use_len[i] = 0;

      t_args_t* out = (t_args_t*)malloc(sizeof(t_args_t));
	   out->data = &data->fdata[data->fpos];

	   int available_bytes = data->flen - data->fpos;
	   if(available_bytes < 0)
		   available_bytes = 0;
      
      out->t_num = i;
	   out->length = (req_bytes < available_bytes)? req_bytes:available_bytes;

	   // Set the length to end at a space
	   for (data->fpos += (long)out->length;
			data->fpos < data->flen && 
			data->fdata[data->fpos] != ' ' && data->fdata[data->fpos] != '\t' &&
			data->fdata[data->fpos] != '\r' && data->fdata[data->fpos] != '\n';
			data->fpos++, out->length++);
	   
			//printf("TID is %d file location is %ld and length is %d %ld\n",i,data->fpos, out->length, data->flen);
			//fflush(stdout);

	   CHECK_ERROR(pthread_create(&tid[i], &attr, wordcount_map, (void*)out) != 0);
   }

   /* Barrier, wait for all threads to finish */
   for (i = 0; i < num_procs; i++)
   {
      int ret_val;
      CHECK_ERROR(pthread_join(tid[i], (void **)(void*)&ret_val) != 0);
	  CHECK_ERROR(ret_val != 0);
   }


   // Join the arrays
   int num_threads = num_procs / 2;
   int rem_num = num_procs % 2;
   
   wc_count_t** mwords = (wc_count_t**)malloc(num_procs*sizeof(wc_count_t*));
   
   while(num_threads > 0)
   {
	   for(i=0; i<num_threads; i++)
	   {
		   merge_data_t* m_args = (merge_data_t*)malloc(sizeof(merge_data_t));
		   m_args->length1 = use_len[i*2];
         m_args->length2 = use_len[i*2 + 1];
         m_args->length_out_pos = i;
         m_args->data1 = words[i*2];
         m_args->data2 = words[i*2 + 1];
         int tlen = m_args->length1 + m_args->length2;
         mwords[i] = (wc_count_t*)malloc(tlen*sizeof(wc_count_t));
         m_args->out = mwords[i];
         
		   CHECK_ERROR(pthread_create(&tid[i], &attr, merge_sections, (void*)m_args) != 0);
	   }

	   /* Barrier, wait for all threads to finish */
	   for (i = 0; i < num_threads; i++)
	   {
		  int ret_val;
		  CHECK_ERROR(pthread_join(tid[i], (void **)(void*)&ret_val) != 0);
		  CHECK_ERROR(ret_val != 0);

		  words[i] = mwords[i];
		  use_len[i] = length[i];
	   }
	   
      if (rem_num == 1)
      {
         words[num_threads] = words[num_threads*2];
         use_len[num_threads] = use_len[num_threads*2];
      }
      
      int old_num = num_threads;
	   num_threads = (num_threads+rem_num) / 2;
      rem_num = (old_num+rem_num) % 2;
   }
   free(length);
   free(mwords);
   free(tid);
}

/** wordcount_map()
 * Go through the allocated portion of the file and count the words
 */
void *wordcount_map(void *args_in) 
{
	t_args_t* args = (t_args_t*)args_in;

   char *curr_start, curr_ltr;
   int state = NOT_IN_WORD;
   int i;
   assert(args);

   char *data = (char *)(args->data);
   curr_start = data;
   assert(data);

   for (i = 0; i < args->length; i++)
   {
      curr_ltr = toupper(data[i]);
      switch (state)
      {
      case IN_WORD:
         data[i] = curr_ltr;
         if ((curr_ltr < 'A' || curr_ltr > 'Z') && curr_ltr != '\'')
         {
            data[i] = 0;
			//printf("\nthe word is %s\n\n",curr_start);
			wordcount_reduce(curr_start, args->t_num);
            state = NOT_IN_WORD;
         }
      break;

      default:
      case NOT_IN_WORD:
         if (curr_ltr >= 'A' && curr_ltr <= 'Z')
         {
            curr_start = &data[i];
            data[i] = curr_ltr;
            state = IN_WORD;
         }
         break;
      }
			
			COZ_PROGRESS;
   }

   // Add the last word
   if (state == IN_WORD)
   {
			data[args->length] = 0;
			//printf("\nthe word is %s\n\n",curr_start);
			wordcount_reduce(curr_start, args->t_num);
   }
   free(args);
   return (void *)0;
}

/** wordcount_reduce()
 * Locate the key in the array of word counts and
 * add up the partial sums for each word
 */
void wordcount_reduce(char* word, int t_num) 
{
   int cmp=-1, high = use_len[t_num], low = -1, next;

   // Binary search the array to find the key
   while (high - low > 1)
   {
       next = (high + low) / 2;   
       cmp = strcmp(word, words[t_num][next].word);
       if (cmp == 0)
       {
          high = next;  
          break;
       }
       else if (cmp < 0)
           high = next;
       else
           low = next;
   }

	int pos = high;

   if (pos >= use_len[t_num])
   {
      // at end
      words[t_num][use_len[t_num]].word = word;
	   words[t_num][use_len[t_num]].count = 1;
	   use_len[t_num]++;
	}
   else if (pos < 0)
   {
      // at front
      memmove(&words[t_num][1], words[t_num], use_len[t_num]*sizeof(wc_count_t));
      words[t_num][0].word = word;
	   words[t_num][0].count = 1;
	   use_len[t_num]++;
   }
   else if (cmp == 0)
   {
      // match
      words[t_num][pos].count++;
	}
   else
   {
      // insert at pos
      memmove(&words[t_num][pos+1], &words[t_num][pos], (use_len[t_num]-pos)*sizeof(wc_count_t));
      words[t_num][pos].word = word;
	   words[t_num][pos].count = 1;
	   use_len[t_num]++;
   }

	if(use_len[t_num] == length[t_num])
	{
		length[t_num] *= 2;
	   words[t_num] = (wc_count_t*)realloc(words[t_num],length[t_num]*sizeof(wc_count_t));
	}
}

/** merge_sections()
 * Merge the partial arrays to create the final array that has the
 * word counts
 */
void *merge_sections(void *args_in)  
{
   merge_data_t* args = (merge_data_t*)args_in;
   int cmp_ret;
   int curr1, curr2;
   int length_out = 0;
   
   for (curr1 = 0, curr2 = 0; 
        curr1 < args->length1 && curr2 < args->length2;) 
   {
      cmp_ret = strcmp(args->data1[curr1].word, args->data2[curr2].word);
      if (cmp_ret == 0)
      {
        //add 
        args->data1[curr1].count += args->data2[curr2].count;
        curr2++;
      }
      else if (cmp_ret < 0)
      {
         // insert args->data1[curr1].word
         memcpy(&args->out[length_out], &args->data1[curr1], sizeof(wc_count_t));
         length_out++;
         curr1++;
      }
      else
      {
         // insert args->data2[curr2].word
         memcpy(&args->out[length_out], &args->data2[curr2], sizeof(wc_count_t));
         length_out++;
         curr2++;
      }
			
			COZ_PROGRESS;
   }
   
   // copy the remaining elements
   memcpy(&args->out[length_out], &args->data1[curr1], (args->length1 - curr1)*sizeof(wc_count_t));
   memcpy(&args->out[length_out], &args->data2[curr2], (args->length2 - curr2)*sizeof(wc_count_t));

   // set length
   length_out += (args->length1 - curr1) + (args->length2 - curr2);
   length[args->length_out_pos] = length_out;
   free(args->data1);
   free(args->data2);
   free(args);
   
   return (void*)0;
}

int main(int argc, char *argv[]) {
   
   int i;
   int fd;
   char * fdata;
   int disp_num;
   struct stat finfo;
   char * fname, * disp_num_str;

   struct timeval starttime,endtime;

   // Make sure a filename is specified
   if (argv[1] == NULL)
   {
      printf("USAGE: %s <filename> [Top # of results to display]\n", argv[0]);
      exit(1);
   }
   
   fname = argv[1];
   disp_num_str = argv[2];

   printf("Wordcount: Running...\n");
   
   // Read in the file
   CHECK_ERROR((fd = open(fname, O_RDONLY)) < 0);
   // Get the file info (for file length)
   CHECK_ERROR(fstat(fd, &finfo) < 0);
   // Memory map the file
   CHECK_ERROR((fdata = (char*)mmap(0, finfo.st_size + 1, 
      PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0)) == NULL);
   
   // Get the number of results to display
   CHECK_ERROR((disp_num = (disp_num_str == NULL) ? 
      DEFAULT_DISP_NUM : atoi(disp_num_str)) <= 0);
   
   // Setup splitter args
   wc_data_t wc_data;
   wc_data.unit_size = 5; // approx 5 bytes per word
   wc_data.fpos = 0;
   wc_data.flen = finfo.st_size;
   wc_data.fdata = fdata;

   dprintf("Wordcount: Calling MapReduce Scheduler Wordcount\n");

   gettimeofday(&starttime,0);

   wordcount_splitter(&wc_data);
   

   gettimeofday(&endtime,0);

   printf("Word Count: Completed %ld\n",(endtime.tv_sec - starttime.tv_sec));

   gettimeofday(&starttime,0);

   sort_pthreads(words[0], use_len[0], sizeof(wc_count_t), wordcount_cmp);

   gettimeofday(&endtime,0);

	dprintf("Word Count: Sorting Completed %ld\n",(endtime.tv_sec - starttime.tv_sec));

   for(i=0; i< DEFAULT_DISP_NUM && i < use_len[0] ; i++)
   {
		wc_count_t* temp = &(words[0][i]);
		printf("The word is %s and count is %d\n", temp->word, temp->count);
		//fflush(stdout);
   }
   free(use_len);
   free(words[0]);
   free(words);

   CHECK_ERROR(munmap(fdata, finfo.st_size + 1) < 0);
   CHECK_ERROR(close(fd) < 0);

   return 0;
}
