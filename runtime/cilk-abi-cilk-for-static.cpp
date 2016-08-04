/* cilk-abi-cilk-for-static.cpp                  -*-C++-*-
 *
 *************************************************************************
 *
 *  Copyright (C) 2011-2015, Intel Corporation
 *  All rights reserved.
 *  
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in
 *      the documentation and/or other materials provided with the
 *      distribution.
 *    * Neither the name of Intel Corporation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *  
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 *  AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
 *  WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *  
 *  *********************************************************************
 *  
 *  PLEASE NOTE: This file is a downstream copy of a file mainitained in
 *  a repository at cilkplus.org. Changes made to this file that are not
 *  submitted through the contribution process detailed at
 *  http://www.cilkplus.org/submit-cilk-contribution will be lost the next
 *  time that a new version is released. Changes only submitted to the
 *  GNU compiler collection or posted to the git repository at
 *  https://bitbucket.org/intelcilkplusruntime/itnel-cilk-runtime.git are
 *  not tracked.
 *  
 *  We welcome your contributions to this open source project. Thank you
 *  for your assistance in helping us improve Cilk Plus.
 *
 **************************************************************************/

/* Implementation of cilk_for ABI.
 *
 * This file must be C++, not C, in order to handle C++ exceptions correctly
 * from within the body of the cilk_for loop
 */
#include <unistd.h>
#include <cstring>
#include <algorithm>

extern "C" {
#include "internal/abi.h"
#include "metacall_impl.h"
#include "global_state.h"
#include "signal_node.h"
#include <cilk/hyperobject_base.h>

/** added by mahwish**/
#define MASTER 0
#include<stdio.h>
#include <sched.h>
//#include <string.h>
#include <pthread.h>
//#include <errno.h>
//#include <iostream>
//#include <cstdlib>
/*********************/
// Icky macros to determine if we're compiled with optimization.  Based on
// the declaration of __CILKRTS_ASSERT in common.h
#if defined(_WIN32)
# if defined (_DEBUG)
#   define CILKRTS_OPTIMIZED 0    // Assumes /MDd is always used with /Od
# else
#   define CILKRTS_OPTIMIZED 1
# endif // defined(_DEBUG)
#else
# if defined(__OPTIMIZE__)
#   define CILKRTS_OPTIMIZED 1
# else
#   define CILKRTS_OPTIMIZED 0
# endif
#endif

#if WITH_PREFETCH
#define CPU_PREFETCH_ex(cache_line) \
{ \
    __asm__ __volatile__ ("prefetcht0 %0" : : "m" (*(pp_exec_t*)cache_line)); \
}
#define CPU_PREFETCH_id(cache_line) \
{ \
    __asm__ __volatile__ ("prefetcht0 %0" : : "m" (*(pp_exec_count*)cache_line)); \
}
#define CPU_PREFETCH_par(cache_line) \
{ \
    __asm__ __volatile__ ("prefetcht0 %0" : : "m" (*(struct param*)cache_line)); \
}
#define CPU_PREFETCH_tree(cache_line) \
{ \
    __asm__ __volatile__ ("prefetcht0 %0" : : "m" (*(struct tree_struct*)cache_line)); \
}
#endif // WITH_PREFETCH

typedef unsigned pp_exec_id_t;
struct pp_exec_t {
    __cilk_abi_f32_t __unused_func;
    void * __unused_arg;
    pp_exec_id_t __unused_t_id;
    //int argc;
    bool control;
    char pad[30];
};
struct pp_exec_count{
  bool xid;
  char padd[63];

};
struct param {
    cilk32_t low32;
    cilk32_t high32;
    cilk64_t low64;
    cilk64_t high64;
    uint64_t * __unused_out;
    int __unused_off;
    char * __unused_data;
    char padd[8];
};
struct tree_struct{
   int t_id;
   int tree_size;
   int num_children;
   int child[2];
   uint64_t start_i;
   uint64_t stop_i;
   uint64_t delta;
   char padd[12];
};
typedef struct pp_exec_t pp_exec_t;
typedef struct pp_exec_count pp_exec_count;

struct capture_data {
    void *body;
    void *data;
    bool is32f;
};

static pp_exec_t *pp_exec;
static pp_exec_count *pp_exec_c;
static struct param *params;
static int *id;
static struct tree_struct *pp_tree;
int nthreads;
static int num_socket;
static int delta_socket;
static int cores_per_socket;
static struct capture_data capture;

#define WITH_REDUCERS 1
#if WITH_REDUCERS
#define CACHE_BLOCK_SIZE 64
static __cilkrts_hyperobject_base *hypermap_reducer;
static char *hypermap_views;
static __thread void *hypermap_local_view;

// Round-up view size to multiple of cache block size
#define HYPERMAP_VIEW_SIZE  (hypermap_reducer->__view_size + ((hypermap_reducer->__view_size + CACHE_BLOCK_SIZE-1) & (CACHE_BLOCK_SIZE-1)))
#define HYPERMAP_SIZE  (HYPERMAP_VIEW_SIZE * nthreads)
#define HYPERMAP_GET_WORKER(tid) (&hypermap_views[(tid)*HYPERMAP_VIEW_SIZE])
#define HYPERMAP_GET_MASTER (void*)(((char*)hypermap_reducer)+hypermap_reducer->__view_offset)
#define HYPERMAP_GET(tid) (tid==MASTER?HYPERMAP_GET_MASTER:HYPERMAP_GET_WORKER(tid))
#endif

static bool __init_parallel=false;

static bool is_up = false;

static void pp_exec_init( pp_exec_t * x, pp_exec_count *c )
{
    c->xid = false;
   // x->argc =0;
  
}
static void pp_tree_init(struct tree_struct * t, int tree_size, int t_id)
{
    int subtree=0;
    t->t_id= t_id;
    if(tree_size <2){	/*leaf node*/
         t->num_children=0;
    }
    else{
        t->num_children= tree_size<3 ? 1: 2;
	t->child[0]=  t_id+1;	/*left child id*/
	subtree= tree_size/2;	/*left subtree size*/
	pp_tree_init( &pp_tree[t->child[0]], subtree, t->child[0]);
	if(t->num_children>1){
	    t->child[1]= t_id+(tree_size/2)+1; /*right child id*/
	    subtree= (tree_size-1)- subtree;	/*right subtree size*/
	    pp_tree_init( &pp_tree[t->child[1]], subtree, t->child[1] );
	}
    }
}

static void
pp_exec_submit(int id)
{
    // bool xid;
    pp_exec_t *x= &pp_exec[id];
    pp_exec_count *c= &pp_exec_c[id];
    // xid= c->xid;
    c->xid= true;
    // assert(xid != c->xid);
}
static void asm_pause()
{
    // should help hyperthreads
    // __asm__ __volatile__( "pause" : : : );
}
static void pp_exec_wait( int id )
{
   pp_exec_count *c= &pp_exec_c[id];
   while( c->xid==true) sched_yield();
}

static void
dummy_fn( void * )
{
    while( 1 ) pause();
}

void
static_scheduler_fn( int tid, signal_node_t * dyn_snode )
{
    pp_exec_t *x = &pp_exec[tid];
    pp_exec_count *c = &pp_exec_c[tid];
    struct tree_struct *p_tree= &pp_tree[tid];
    struct param *p= &params[tid];
    int num_children;
    int j;

    if( ! __init_parallel )
        return;

    // printf( "static sched tid=%d\n", tid );

    num_children = p_tree->num_children;
    while( 1 ) {
	/* Busy wait loop for fast response */
        while( *((volatile bool*)&c->xid) == false) {
	    // We are polling Cilk's thread signal node. If it says wait,
	    // we run, if it says continue, we stop.
	    if( !signal_node_should_wait( dyn_snode ) ) {
		// printf( "static sched tid=%d leaving\n", tid );
		return;
	    }
	    sched_yield();
	    asm_pause();
	}
#if WITH_PREFETCH
        if(x->control==true){
	   CPU_PREFETCH_ex(&pp_exec[tid]);
	   CPU_PREFETCH_id(&pp_exec_c[tid]);
	   CPU_PREFETCH_tree(&pp_tree[tid]);
	    CPU_PREFETCH_par(&pp_tree[tid]);
           for(j=tid+1; j < tid+delta_socket && j< nthreads; j++){
		CPU_PREFETCH_ex(&pp_exec[j]);
		CPU_PREFETCH_id(&pp_exec_c[j]);
		CPU_PREFETCH_tree(&pp_tree[j]);
		CPU_PREFETCH_par(&pp_tree[j]);
           }
        }
#endif // WITH_PREFETCH

       // Signal other threads to get going
        for(j=0; j<num_children; j++) {
	    pp_exec_submit( p_tree->child[j] );
        }

#if WITH_REDUCERS
       // If we have reducers in this loop, initialize them here
       if( hypermap_reducer ) {
           hypermap_local_view = HYPERMAP_GET(tid);
           (*hypermap_reducer->__c_monoid.identity_fn)(
               (void*)&hypermap_reducer->__c_monoid, HYPERMAP_GET(tid) );
       }
#endif

       // Execute loop body
       if( capture.is32f ) {
           __cilk_abi_f32_t body32
               = reinterpret_cast<__cilk_abi_f32_t>(capture.body);
           (*body32)( capture.data, p->low32, p->high32 );
       } else {
           __cilk_abi_f64_t body64
               = reinterpret_cast<__cilk_abi_f64_t>(capture.body);
           (*body64)( capture.data, p->low64, p->high64 );
       }

       // Wait for other threads to complete
       for(j=0; j<num_children; j++) {
           pp_exec_wait(p_tree->child[j]);
#if WITH_REDUCERS
          if( hypermap_reducer ) {
              // Reduce results from other thread
              (*hypermap_reducer->__c_monoid.reduce_fn)(
                  (void*)&hypermap_reducer->__c_monoid,
                  HYPERMAP_GET(tid), HYPERMAP_GET(p_tree->child[j]) );
              // Destroy other thread's view
              (*hypermap_reducer->__c_monoid.destroy_fn)(
                  (void*)&hypermap_reducer->__c_monoid,
                  HYPERMAP_GET(p_tree->child[j]) );
          }
#endif
	}
	/* Signal we're done */
        c->xid= false;
    } //end if while
}

static void
pp_exec_fn( int* id )
{
    static_scheduler_fn( *id, 0 );
}



void __parallel_initialize(void){
   pthread_t tid;
   int i,j, nid, ret;

   /* Do this only once */
   if( __init_parallel )
       return;

   if(const char* env_n = getenv("CILK_NWORKERS")){
       nthreads= atoi(env_n);
   }
   else{
       nthreads=1;
   }

   if(const char* env_n = getenv("MACHINE")){
       num_socket= atoi(env_n);
      if(strcmp(env_n,"jacob")==0){
        cores_per_socket=12; 
      }
      else if(strcmp(env_n,"hvan03")==0){
         cores_per_socket=8;
      }
      else if(strcmp(env_n,"hpdc")==0){
         cores_per_socket=8;
      }
   }else{
      cores_per_socket=12; 
   }


   /*calculate over how many sockets the threds will be spread, and how many threads/socket*/
   num_socket= (nthreads+cores_per_socket-1)/cores_per_socket;  //assumption: correct number of threads given
   delta_socket= (nthreads + num_socket-1)/ num_socket;
   /*allocate memory for data structures*/
  // pp_tree= (struct tree_struct *) malloc(nthreads * sizeof(struct tree_struct));
   pp_exec = new pp_exec_t[nthreads];
   pp_exec_c = new pp_exec_count[nthreads];
   id= new int[nthreads];
   pp_tree= new struct tree_struct[nthreads];
   params= new struct param[nthreads];
  /* tree structure initialisation*/
   for (j=0, nid=0; j<num_socket; nid+=delta_socket, j++){
      int rem= nthreads-nid;
      int size= std::min(rem, delta_socket);
      pp_tree_init(&pp_tree[nid],size,nid);
   } 

   //printf("size of pp_exec_t: %d\n", sizeof(pp_exec_t));
   for( i=0; i < nthreads; ++i ) {
        id[i] =i;
	pp_exec_c[i].xid=false;
        pp_exec[i].control= false;

#if 0
        if( i==0 ) {
           // Create dummy thread
           if( (ret=pthread_create( &tid, NULL, (void *(*)(void *))dummy_fn, NULL )) )
           {
               fprintf( stderr, "Error creating thread %d (dummy), retcode=%d: %s\n",
                        i, ret, strerror(ret) );
               exit( 1 );
           }
           continue;
        }

	if( (ret=pthread_create( &tid, NULL, (void *(*)(void *))pp_exec_fn, (void *)&id[i] )) )
	{
           fprintf( stderr, "Error creating thread %d, retcode=%d: %s\n",
                    i, ret, strerror(ret) );
	    exit( 1 );
	}
#endif

    }
    __init_parallel = true;
    is_up = true;
    printf( "runtime up...\n" );
}

} // extern "C"

template<typename F>
struct is_32f;

template<>
struct is_32f<__cilk_abi_f32_t> {
    static const bool value = true;
};

template<>
struct is_32f<__cilk_abi_f64_t> {
    static const bool value = false;
};

/*
 * cilk_for_root
 *
 * Templatized function to implement the top level of a cilk_for loop.
 *
 * body  - lambda function for the cilk_for loop body
 * data  - data used by the lambda function
 * count - trip count for loop
 * grain - grain size (0 if it should be computed)
 */
#if WITH_REDUCERS
template <typename count_t, typename F>
static void cilk_for_root_static(F body, void *data, count_t count, int grain,
                                __cilkrts_hyperobject_base *hypermap)
#else
template <typename count_t, typename F>
static void cilk_for_root_static(F body, void *data, count_t count, int grain)
#endif
{
   /******************************IMPLEMENTATION BY MAHWISH*****************************/
    int j, nid, num_children;
    if( ! __init_parallel )
        __parallel_initialize();
    // printf("CILK-STATIC SCHEDULER-OPTIMIZED- number of threads=%d\n", nthreads);

#if WITH_REDUCERS
     // could use class storage_for_object<> from cilk headers
     hypermap_reducer = hypermap;
     char hypermap_buffer[hypermap ? HYPERMAP_SIZE+CACHE_BLOCK_SIZE:1];

     if( hypermap ) {
        // Stack-allocate views.
        uintptr_t offset = uintptr_t(&hypermap_buffer[0])
            & uintptr_t(CACHE_BLOCK_SIZE-1);
        // printf( "view size: %ld * %d = %ld\n",
        //         HYPERMAP_VIEW_SIZE, nthreads, HYPERMAP_SIZE );
        hypermap_views = &hypermap_buffer[CACHE_BLOCK_SIZE-offset];
        assert( (uintptr_t(hypermap_views) & uintptr_t(CACHE_BLOCK_SIZE-1)) == 0 );
     } else {
        hypermap_views = NULL;
     }

     // Initialize pointer to our local view
     hypermap_local_view = hypermap_reducer ? HYPERMAP_GET(MASTER) : 0;
#endif

    capture.body = reinterpret_cast<void *>(body);
    capture.data = data;
    capture.is32f = is_32f<F>::value;
    if( capture.is32f ) {
       count_t delta = (count+nthreads-1)/nthreads;
       for (int i=0; i<nthreads; i++){
           params[i].low32 = std::min((delta)*i,count);
           params[i].high32 = std::min(delta*(i+1),count);
       }
    } else {
       count_t delta = (count+nthreads-1)/nthreads;
       for (int i=0; i<nthreads; i++){
           params[i].low64 = std::min((delta)*i,count);
           params[i].high64 = std::min(delta*(i+1),count);
       }
    }

    /* store fence if not TSO/x86-64 */

   /* first distribute across socket */
   for (j=1,  nid=delta_socket; j<num_socket; nid+=delta_socket, j++){
      pp_exec[nid].control= true;
      pp_exec_submit( nid );
   } 
    /* now distribute within socket in a tree-like manner */
    num_children = pp_tree[MASTER].num_children;
    for(int i=0; i<num_children; i++){
	pp_exec_submit( pp_tree[MASTER].child[i] );
    }

    /* now go on and do the work */
    if( capture.is32f )
       (*body)( capture.data, params[MASTER].low32, params[MASTER].high32 );
    else
       (*body)( capture.data, params[MASTER].low64, params[MASTER].high64 );


    // TODO: the way we reduce results supports non-commutative
    //       reducers only if we get the tree right!

    /* wait on child nodes to finish work*/
    for(int i=0; i<num_children; i++) {
        pp_exec_wait(pp_tree[MASTER].child[i]);
#if WITH_REDUCERS
       if( hypermap_reducer ) {
          // Reduce results from other thread
          (*hypermap_reducer->__c_monoid.reduce_fn)(
              (void*)&hypermap_reducer->__c_monoid,
              HYPERMAP_GET(MASTER), HYPERMAP_GET(pp_tree[MASTER].child[i]) );
          // Destroy other thread's view
          (*hypermap_reducer->__c_monoid.destroy_fn)(
              (void*)&hypermap_reducer->__c_monoid,
              HYPERMAP_GET(pp_tree[MASTER].child[i]) );
       }
#endif
    }

    for (int j=1, nid=delta_socket; j<num_socket; nid+=delta_socket, j++){
#if WITH_REDUCERS
       if( hypermap_reducer ) {
          // Reduce results from other thread
          (*hypermap_reducer->__c_monoid.reduce_fn)(
              (void*)&hypermap_reducer->__c_monoid,
              HYPERMAP_GET(MASTER), HYPERMAP_GET(nid) );
          // Destroy other thread's view
          (*hypermap_reducer->__c_monoid.destroy_fn)(
              (void*)&hypermap_reducer->__c_monoid, HYPERMAP_GET(nid) );
       }
#endif
    }

#if WITH_REDUCERS
    // The master always uses the leftmost view.
    // The master has thus applied all updates to the final view.
    // Unset variables
    hypermap_reducer = 0;
    hypermap_views = 0;
#endif

    // printf("CILK-STATIC SCHEDULER-OPTIMIZED- done\n" );
}

// Use extern "C" to suppress name mangling of __cilkrts_cilk_for_32 and
// __cilkrts_cilk_for_64.
extern "C" {

/*
 * __cilkrts_cilk_for_32
 *
 * Implementation of cilk_for for 32-bit trip counts (regardless of processor
 * word size).  Assumes that the range is 0 - count.
 *
 * body  - lambda function for the cilk_for loop body
 * data  - data used by the lambda function
 * count - trip count for loop
 * grain - grain size (0 if it should be computed)
 */

CILK_ABI_THROWS_VOID
__cilkrts_cilk_for_static_32(__cilk_abi_f32_t body, void *data,
			     cilk32_t count, int grain)
{
    // Cilkscreen should not report this call in a stack trace
    NOTIFY_ZC_INTRINSIC((char *)"cilkscreen_hide_call", 0);
    // Check for an empty range here as an optimization - don't need to do any
    // __cilkrts_stack_frame initialization
#if WITH_REDUCERS
    if (count > 0)
        cilk_for_root_static(body, data, count, grain, /*hypermap*/NULL);
#else
    if (count > 0)
        cilk_for_root_static(body, data, count, grain);
#endif
}

#if WITH_REDUCERS
/*
 * __cilkrts_cilk_for_static_reduce_32
 *
 * Implementation of cilk_for for 32-bit trip counts (regardless of processor
 * word size).  Assumes that the range is 0 - count.
 *
 * body  - lambda function for the cilk_for loop body
 * data  - data used by the lambda function
 * count - trip count for loop
 * grain - grain size (0 if it should be computed)
 * hypermap - structure describe all relevant hypermap functions and its size
 */

CILK_ABI_THROWS_VOID
__cilkrts_cilk_for_static_reduce_32(__cilk_abi_f32_t body, void *data,
                                   cilk32_t count, int grain,
                                   __cilkrts_hyperobject_base *hypermap)
{
    // Cilkscreen should not report this call in a stack trace
    NOTIFY_ZC_INTRINSIC((char *)"cilkscreen_hide_call", 0);
    // Check for an empty range here as an optimization - don't need to do any
    // __cilkrts_stack_frame initialization
    if (count > 0)
        cilk_for_root_static(body, data, count, grain, hypermap);
}

#if 0
CILK_ABI_THROWS_VOID __cilkrts_cilk_for_numa_32(__cilk_abi_f32_t body, void *data,
                                               cilk32_t count, int grain) {
    __cilkrts_cilk_for_static_reduce_32( body, data, count, grain , 0 );
}
#endif

CILK_EXPORT void* __CILKRTS_STRAND_PURE(
    __cilkrts_hyper_lookup_static(__cilkrts_hyperobject_base *key))
{
    return hypermap_local_view;
}
#endif

/*
 * __cilkrts_cilk_for_64
 *
 * Implementation of cilk_for for 64-bit trip counts (regardless of processor
 * word size).  Assumes that the range is 0 - count.
 *
 * body  - lambda function for the cilk_for loop body
 * data  - data used by the lambda function
 * count - trip count for loop
 * grain - grain size (0 if it should be computed)
 */
CILK_ABI_THROWS_VOID
__cilkrts_cilk_for_static_64(__cilk_abi_f64_t body, void *data,
			     cilk64_t count, int grain)
{
    // Cilkscreen should not report this call in a stack trace
    NOTIFY_ZC_INTRINSIC((char *)"cilkscreen_hide_call", 0);
    // Check for an empty range here as an optimization - don't need to do any
    // __cilkrts_stack_frame initialization
#if WITH_REDUCERS
    if (count > 0)
        cilk_for_root_static(body, data, count, grain, /*hypermap*/0);
#else
    if (count > 0)
        cilk_for_root_static(body, data, count, grain);
#endif
}

#if WITH_REDUCERS
CILK_ABI_THROWS_VOID
__cilkrts_cilk_for_static_reduce_64(__cilk_abi_f64_t body, void *data,
                                   cilk64_t count, int grain,
                                   __cilkrts_hyperobject_base *hypermap)
{
    // Cilkscreen should not report this call in a stack trace
    NOTIFY_ZC_INTRINSIC((char *)"cilkscreen_hide_call", 0);
    // Check for an empty range here as an optimization - don't need to do any
    // __cilkrts_stack_frame initialization
    if (count > 0)
        cilk_for_root_static(body, data, count, grain, hypermap);
}
#endif // WITH_REDUCERS

#if 0
CILK_ABI_THROWS_VOID __cilkrts_cilk_for_numa_64(__cilk_abi_f64_t body, void *data,
                                               cilk64_t count, int grain) {
    __cilkrts_cilk_for_static_reduce_64( body, data, count, grain , 0 );
}
#endif


}// end extern "C"



/* End cilk-abi-cilk-for-static.cpp */
