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

#define DEF_NUM_POINTS 100000
#define DEF_NUM_MEANS 100
#define DEF_DIM 3
#define DEF_GRID_SIZE 1000

#define false 0
#define true 1

int num_points; // number of vectors
int dim;       // Dimension of each vector
int num_means; // number of clusters
int grid_size; // size of each dimension of vector space
int modified;
int num_pts = 0;
   
int **points;
int **means;
int *clusters;

typedef struct {
   int start_idx;
   int num_pts;
   int *sum;
} thread_arg;

/** dump_points()
 *  Helper function to print out the points
 */
void dump_points(int **vals, int rows)
{
   int i, j;
   
   for (i = 0; i < rows; i++) 
   {
      for (j = 0; j < dim; j++)
      {
         dprintf("%5d ",vals[i][j]);
      }
      dprintf("\n");
   }
}

/** parse_args()
 *  Parse the user arguments
 */
void parse_args(int argc, char **argv) 
{
   int c;
   extern char *optarg;
   extern int optind;
   
   num_points = DEF_NUM_POINTS;
   num_means = DEF_NUM_MEANS;
   dim = DEF_DIM;
   grid_size = DEF_GRID_SIZE;
   
   while ((c = getopt(argc, argv, "d:c:p:s:")) != EOF) 
   {
      switch (c) {
         case 'd':
            dim = atoi(optarg);
            break;
         case 'c':
            num_means = atoi(optarg);
            break;
         case 'p':
            num_points = atoi(optarg);
            break;
         case 's':
            grid_size = atoi(optarg);
            break;
         case '?':
            printf("Usage: %s -d <vector dimension> -c <num clusters> -p <num points> -s <grid size>\n", argv[0]);
            exit(1);
      }
   }
   
   if (dim <= 0 || num_means <= 0 || num_points <= 0 || grid_size <= 0) {
      printf("Illegal argument value. All values must be numeric and greater than 0\n");
      exit(1);
   }
   
   printf("Dimension = %d\n", dim);
   printf("Number of clusters = %d\n", num_means);
   printf("Number of points = %d\n", num_points);
   printf("Size of each dimension = %d\n", grid_size);   
}

/** generate_points()
 *  Generate the points
 */
void generate_points(int **pts, int size) 
{   
   int i, j;
   
   for (i=0; i<size; i++) 
   {
      for (j=0; j<dim; j++) 
      {
         pts[i][j] = rand() % grid_size;
      }
   }
}

/** get_sq_dist()
 *  Get the squared distance between 2 points
 */
static inline unsigned int get_sq_dist(int *v1, int *v2)
{
   int i;
   
   unsigned int sum = 0;
   for (i = 0; i < dim; i++) 
   {
      sum += ((v1[i] - v2[i]) * (v1[i] - v2[i])); 
   }
   return sum;
}

/** add_to_sum()
 *	Helper function to update the total distance sum
 */
void add_to_sum(int *sum, int *point)
{
   int i;
   
   for (i = 0; i < dim; i++)
   {
      sum[i] += point[i];   
   }   
}

/** find_clusters()
 *  Find the cluster that is most suitable for a given set of points
 */
void *find_clusters(void *arg) 
{
   thread_arg *t_arg = (thread_arg *)arg;
   int i, j;
   unsigned int min_dist, cur_dist;
   int min_idx;
   int start_idx = t_arg->start_idx;
   int end_idx = start_idx + t_arg->num_pts;
   
   for (i = start_idx; i < end_idx; i++) 
   {
      min_dist = get_sq_dist(points[i], means[0]);
      min_idx = 0; 
      for (j = 1; j < num_means; j++)
      {
         cur_dist = get_sq_dist(points[i], means[j]);
         if (cur_dist < min_dist) 
         {
            min_dist = cur_dist;
            min_idx = j;   
         }
      }
      
      if (clusters[i] != min_idx) 
      {
         clusters[i] = min_idx;
         modified = true;
      }
			
			COZ_PROGRESS_NAMED("clusters found");
   }
   
   return (void *)0;   
}

/** calc_means()
 *  Compute the means for the various clusters
 */
void *calc_means(void *arg)
{
   int i, j, grp_size;
   int *sum;
   thread_arg *t_arg = (thread_arg *)arg;
   int start_idx = t_arg->start_idx;
   int end_idx = start_idx + t_arg->num_pts;
   
   sum = t_arg->sum;
   
   for (i = start_idx; i < end_idx; i++) 
   {
      memset(sum, 0, dim * sizeof(int));
      grp_size = 0;
      
      for (j = 0; j < num_points; j++)
      {
         if (clusters[j] == i) 
         {
            add_to_sum(sum, points[j]);
            grp_size++;
         }   
      }
      
      for (j = 0; j < dim; j++)
      {
         //dprintf("div sum = %d, grp size = %d\n", sum[j], grp_size);
         if (grp_size != 0)
         { 
            means[i][j] = sum[j] / grp_size;
         }
      }
   }
   free(sum);
   return (void *)0;
}

int main(int argc, char **argv)
{
   
   int num_procs, curr_point;
   int i;
   pthread_t *pid;
   pthread_attr_t attr;
   thread_arg *arg;
   int num_per_thread, excess; 
   
   parse_args(argc, argv);   
   
   points = (int **)malloc(sizeof(int *) * num_points);
   for (i=0; i<num_points; i++) 
   {
      points[i] = (int *)malloc(sizeof(int) * dim);
   }
   dprintf("Generating points\n");
   generate_points(points, num_points);
   
   means = (int **)malloc(sizeof(int *) * num_means);
   for (i=0; i<num_means; i++) 
   {
      means[i] = (int *)malloc(sizeof(int) * dim);
   }
   dprintf("Generating means\n");
   generate_points(means, num_means);
 
   clusters = (int *)malloc(sizeof(int) * num_points);
   memset(clusters, -1, sizeof(int) * num_points);
   
   
   pthread_attr_init(&attr);
   pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
   CHECK_ERROR((num_procs = sysconf(_SC_NPROCESSORS_ONLN)) <= 0);
      
   CHECK_ERROR( (pid = (pthread_t *)malloc(sizeof(pthread_t) * num_procs)) == NULL);
   
   modified = true; 
   
   printf("Starting iterative algorithm\n");
   
   /* Create the threads to process the distances between the various
   points and repeat until modified is no longer valid */
   int num_threads;   
   while (modified) 
   {
      num_per_thread = num_points / num_procs;
      excess = num_points % num_procs;
      modified = false;
      dprintf(".");
      curr_point = 0;
      num_threads = 0;
      
      while (curr_point < num_points) {
         CHECK_ERROR((arg = (thread_arg *)malloc(sizeof(thread_arg))) == NULL);
         arg->start_idx = curr_point;
         arg->num_pts = num_per_thread;
         if (excess > 0) {
            arg->num_pts++;
            excess--;            
         }
         CHECK_ERROR((pthread_create(&(pid[num_threads++]), &attr, find_clusters,
                                                   (void *)(arg))) != 0);
         curr_point += arg->num_pts;
      }
      
      assert (num_threads == num_procs);
      for (i = 0; i < num_threads; i++) {
         pthread_join(pid[i], NULL);   
      }
      
      num_per_thread = num_means / num_procs;
      excess = num_means % num_procs;
      curr_point = 0;
      num_threads = 0;
      while (curr_point < num_means) {
         CHECK_ERROR((arg = (thread_arg *)malloc(sizeof(thread_arg))) == NULL);
         arg->start_idx = curr_point;
         arg->sum = (int *)malloc(dim * sizeof(int));
         arg->num_pts = num_per_thread;
         if (excess > 0) {
            arg->num_pts++;
            excess--;            
         }
         CHECK_ERROR((pthread_create(&(pid[num_threads++]), &attr, calc_means,
                                                   (void *)(arg))) != 0);
         curr_point += arg->num_pts;
      }
      
      assert (num_threads == num_procs);
      for (i = 0; i < num_threads; i++) {
         pthread_join(pid[i], NULL);   
      }
      
   }
   
      
   dprintf("\n\nFinal means:\n");
   dump_points(means, num_means);

   for (i = 0; i < num_points; i++) 
      free(points[i]);
   free(points);
   
   for (i = 0; i < num_means; i++) 
   {
      free(means[i]);
   }
   free(means);
   free(clusters);

   return 0;
}
