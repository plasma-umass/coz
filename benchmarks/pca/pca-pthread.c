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
#include <string.h>
#include <math.h>
#include <pthread.h>
#include "stddefines.h"

#include "coz.h"

#define DEF_GRID_SIZE 1000  // all values in the matrix are from 0 to this value 
#define DEF_NUM_ROWS 3000
#define DEF_NUM_COLS 3000

int num_rows;
int num_cols;
int grid_size;
int num_procs;
int next_row;

int **matrix, **cov;
int *mean;
pthread_mutex_t row_lock;

/* Structure that stores the rows
which each thread is supposed to process */
typedef struct {
   int first_row;
   int last_row;
} mean_arg_t;

/** parse_args()
 *  Parse the user arguments to determine the number of rows and colums
 */   
void parse_args(int argc, char **argv) 
{
   int c;
   extern char *optarg;
   extern int optind;
   
   num_rows = DEF_NUM_ROWS;
   num_cols = DEF_NUM_COLS;
   grid_size = DEF_GRID_SIZE;
   
   while ((c = getopt(argc, argv, "r:c:s:")) != EOF) 
   {
      switch (c) {
         case 'r':
            num_rows = atoi(optarg);
            break;
         case 'c':
            num_cols = atoi(optarg);
            break;
         case 's':
            grid_size = atoi(optarg);
            break;
         case '?':
            printf("Usage: %s -r <num_rows> -c <num_cols> -s <max value>\n", argv[0]);
            exit(1);
      }
   }
   
   if (num_rows <= 0 || num_cols <= 0 || grid_size <= 0) {
      printf("Illegal argument value. All values must be numeric and greater than 0\n");
      exit(1);
   }

   printf("Number of rows = %d\n", num_rows);
   printf("Number of cols = %d\n", num_cols);
   printf("Max value for each element = %d\n", grid_size);   
}

/** dump_points()
 *  Print the values in the matrix to the screen
 */
void dump_points(int **vals, int rows, int cols)
{
   int i, j;
   
   for (i = 0; i < rows; i++) 
   {
      for (j = 0; j < cols; j++)
      {
         dprintf("%5d ",vals[i][j]);
      }
      dprintf("\n");
   }
}

/** generate_points()
 *  Create the values in the matrix
 */
void generate_points(int **pts, int rows, int cols) 
{   
   int i, j;
   
   for (i=0; i<rows; i++) 
   {
      for (j=0; j<cols; j++) 
      {
         pts[i][j] = rand() % grid_size;
      }
   }
}

/** calc_mean()
 *  Compute the mean for the rows allocated to a thread
 */
void *calc_mean(void *arg) {
   int i, j;
   int sum = 0;
   mean_arg_t *mean_arg = (mean_arg_t *)arg;
   
   for (i = mean_arg->first_row; i < mean_arg->last_row; i++) {
      sum = 0;
      for (j = 0; j < num_cols; j++) {
         sum += matrix[i][j];
      }
      mean[i] = sum / num_cols;   
			COZ_PROGRESS;
   }
   
   return (void *)0;
}

/** calc_cov()
 *  Calculate the covariance for the portion of the
 *  matrix allocated to a thread. Locking is reuqired
 */
void *calc_cov(void *arg) {
   int i, j, k;
   int sum;
   
   pthread_mutex_lock(&row_lock);
   i = next_row;
   next_row++;
   pthread_mutex_unlock(&row_lock);
   
   while (i < num_rows) {
      for (j = i; j < num_rows; j++) {
         sum = 0;
         for (k = 0; k < num_cols; k++) {
            sum = sum + ((matrix[i][k] - mean[i]) * (matrix[j][k] - mean[j]));
         }
         cov[i][j] = cov[j][i] = sum/(num_cols-1);
      }
      pthread_mutex_lock(&row_lock);
      i = next_row;
      next_row++;
      pthread_mutex_unlock(&row_lock);
			COZ_PROGRESS;
   }
   
   return (void *)0;   
}

/** pthread_mean()
 *  Creates threads to compute the mean. Each thread computes
 *  the mean for a set of rows
 */
void pthread_mean() {
   pthread_attr_t attr;
   pthread_t * tid;
   int i;
   mean_arg_t *mean_args;

   CHECK_ERROR((num_procs = sysconf(_SC_NPROCESSORS_ONLN)) <= 0);
   printf("The number of processors is %d\n", num_procs);

   tid = (pthread_t *)MALLOC(num_procs * sizeof(pthread_t));
   mean_args = (mean_arg_t *)malloc(num_procs * sizeof(mean_arg_t));
   
   /* Thread must be scheduled systemwide */
   pthread_attr_init(&attr);
   pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
   
   int rows_per_thread = num_rows / num_procs;
   int excess = num_rows - (rows_per_thread * num_procs);
   int curr_row = 0;
   
   /* Assign rows to each thread. One thread per processor */
   for(i=0; i<num_procs; i++){
      mean_args[i].first_row = curr_row;
      mean_args[i].last_row = curr_row + rows_per_thread;
      if (excess > 0) {
            mean_args[i].last_row++;
            excess--;
      }
      curr_row = mean_args[i].last_row;
      CHECK_ERROR(pthread_create(&tid[i], &attr, calc_mean, 
                                              (void *)(&(mean_args[i]))) != 0);
   }

   /* Barrier, wait for all threads to finish */
   for (i = 0; i < num_procs; i++)
   {
      CHECK_ERROR(pthread_join(tid[i], NULL) != 0);
   }
   free(tid);
}

/** pthread_cov()
 *  Creates threads to compute the covariance. Each thread computes
 *  the covariance for a portion of the matrix
 */
void pthread_cov() {
   int i;
   pthread_attr_t attr;
   pthread_t * tid;
   
   pthread_mutex_init(&row_lock, NULL);
   
   /* Thread must be scheduled systemwide */
   pthread_attr_init(&attr);
   pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
   next_row = 0;
   
   tid = (pthread_t *)MALLOC(num_procs * sizeof(pthread_t));
   
   for(i=0; i<num_procs; i++){
      CHECK_ERROR(pthread_create(&tid[i], &attr, calc_cov, NULL) != 0);
   }

   /* Barrier, wait for all threads to finish */
   for (i = 0; i < num_procs; i++) {
      CHECK_ERROR(pthread_join(tid[i], NULL) != 0);
   }
}



int main(int argc, char **argv) {
   
   int i;
   
   parse_args(argc, argv);   
   
   // Create the matrix to store the points
   matrix = (int **)malloc(sizeof(int *) * num_rows);
   for (i=0; i<num_rows; i++) 
   {
      matrix[i] = (int *)malloc(sizeof(int) * num_cols);
   }
   //Generate random values for all the points in the matrix
   generate_points(matrix, num_rows, num_cols);
   
   // Print the points
   dump_points(matrix, num_rows, num_cols);
   
   // Allocate Memory to store the mean and the covariance matrix
   mean = (int *)malloc(sizeof(int) * num_rows);
   cov = (int **)malloc(sizeof(int *) * num_rows);
   for (i=0; i<num_rows; i++) 
   {
      cov[i] = (int *)malloc(sizeof(int) * num_rows);
   }

	 // Compute the mean and the covariance
   pthread_mean();
   pthread_cov();
   
   
   dump_points(cov, num_rows, num_rows);
   
   for (i=0; i<num_rows; i++) 
   {
      free(cov[i]);
      free(matrix[i]);
   } 
   free(mean);
   free(cov);
   free(matrix);
   return 0;
}


