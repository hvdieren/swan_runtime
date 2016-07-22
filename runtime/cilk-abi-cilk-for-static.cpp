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

#include "internal/abi.h"
#include "metacall_impl.h"
#include "global_state.h"


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

pp_exec_t *pp_exec;
pp_exec_count *pp_exec_c;
struct param *params;
int *id;
struct tree_struct *pp_tree;
int nthreads;
int num_socket;
int delta_socket;
int cores_per_socket;

bool __init_parallel=false;


void pp_exec_init( pp_exec_t * x, pp_exec_count *c )
{
    c->xid = false;
    x->func = 0;
    x->arg = 0;
   // x->argc =0;
  
}
void pp_tree_init(struct tree_struct * t, int tree_size, int t_id)
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
pp_exec_submit( int id, __cilk_abi_f32_t func, void * arg)
{
    // bool xid;
    pp_exec_t *x= &pp_exec[id];
    pp_exec_count *c= &pp_exec_c[id];
    x->func= func;
    x->arg = arg;
    // xid= c->xid;
    c->xid= true;
    // assert(xid != c->xid);
}
void pp_exec_wait( int id )
{
   pp_exec_count *c= &pp_exec_c[id];
   while( c->xid==true) sched_yield();
}
void
pp_exec_fn( int* id )
{
    int tid= *id;
    pp_exec_t *x = &pp_exec[tid];
    pp_exec_count *c = &pp_exec_c[tid];
    struct tree_struct *p_tree= &pp_tree[tid];
    struct param *p= &params[tid];
    int num_children= p_tree->num_children;
    int j;
    while( 1 ) {
	/* Busy wait loop for fast response */
        while(c->xid== false) sched_yield();
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
        for(j=0; j<num_children; j++){
           pp_exec_submit( p_tree->child[j], x->func, x->arg);

        }
        (*x->func)(x->arg,p->low,p->high);
	for(j=0; j<num_children; j++){
           pp_exec_wait(p_tree->child[j]); 

	}
	/* Signal we're done */
        c->xid= false;
    } //end if while

}




void __parallel_initialize(void){
   pthread_t tid;
   int i,j, nid, ret;
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
        pp_exec[i].func=0;
        pp_exec[i].arg=0;
	pp_exec_c[i].xid=false;
        pp_exec[i].control= false;

        if (i==0) continue;
	if( (ret=pthread_create( &tid, NULL, (void *(*)(void *))pp_exec_fn, (void *)&id[i] )) )
	{
	    fprintf( stderr, "Error creating thread %d\n", i);
	    exit( 1 );
	}
    
    }
    __init_parallel= true;
}

template <typename count_t>
static inline int grainsize(int req, count_t count)
{
    // A positive requested grain size comes from the user.  A very high grain
    // size risks losing parallelism, but the user told us what they want for
    // grainsize.  Who are we to argue?
    if (req > 0)
        return req;

    // At present, a negative requested grain size is treated the same way as
    // a zero grain size, i.e., the runtime computes the actual grainsize
    // using a hueristic.  In the future, the compiler may give us additional
    // information about the size of the cilk_for body by passing a negative
    // grain size.

    // Avoid generating a zero grainsize, even for empty loops.
    if (count < 1)
        return 1;

    global_state_t* g = cilkg_get_global_state();
    if (g->under_ptool)
    {
        // Grainsize = 1, when running under PIN, and when the grainsize has
        // not explicitly been set by the user.
        return 1;
    }
    else
    {
        // Divide loop count by 8 times the worker count and round up.
        const int Px8 = g->P * 8;
        count_t n = (count + Px8 - 1) / Px8;

        // 2K should be enough to amortize the cost of the cilk_for. Any
        // larger grainsize risks losing parallelism.
        if (n > 2048)
            return 2048;
        return (int) n;  // n <= 2048, so no loss of precision on cast to int
    }
}

/*
 * call_cilk_for_loop_body
 *
 * Centralizes the code to call the loop body.  The compiler should be
 * inlining this code
 *
 * low   - Low loop index we're considering in this portion of the algorithm
 * high  - High loop index we're considering in this portion of the algorithm
 * body  - lambda function for the cilk_for loop body
 * data  - data used by the lambda function
 * w     - __cilkrts_worker we're currently executing on
 * loop_root_pedigree - __cilkrts_pedigree node we generated for the root of
 *         the cilk_for loop to flatten out the internal nodes
 */
template <typename count_t, typename F>
inline static
void call_cilk_for_loop_body(count_t low, count_t high,
                             F body, void *data,
                             __cilkrts_worker *w,
                             __cilkrts_pedigree *loop_root_pedigree)
{
    // Cilkscreen should not report this call in a stack trace
    NOTIFY_ZC_INTRINSIC((char *)"cilkscreen_hide_call", 0);

    // The worker is only valid until the first spawn.  Fetch the
    // __cilkrts_stack_frame out of the worker, since it will be stable across
    // steals.  The sf pointer actually points to the *parent's*
    // __cilkrts_stack_frame, since this function is a non-spawning function
    // and therefore has no cilk stack frame of its own.
    __cilkrts_stack_frame *sf = w->current_stack_frame;

    // Save the pedigree node pointed to by the worker.  We'll need to restore
    // that when we exit since the spawn helpers in the cilk_for call tree
    // will assume that it's valid
    const __cilkrts_pedigree *saved_next_pedigree_node = w->pedigree.parent;

    // Add the leaf pedigree node to the chain. The parent is the root node
    // to flatten the tree regardless of the DAG branches in the cilk_for
    // divide-and-conquer recursion.
    //
    // The rank is initialized to the low index.  The user is
    // expected to call __cilkrts_bump_loop_rank at the end of the cilk_for
    // loop body.
    __cilkrts_pedigree loop_leaf_pedigree;

    loop_leaf_pedigree.rank = (uint64_t)low;
    loop_leaf_pedigree.parent = loop_root_pedigree;

    // The worker's pedigree always starts with a rank of 0
    w->pedigree.rank = 0;
    w->pedigree.parent = &loop_leaf_pedigree;

    // Call the compiler generated cilk_for loop body lambda function
    body(data, low, high);

    // The loop body may have included spawns, so we must refetch the worker
    // from the __cilkrts_stack_frame, which is stable regardless of which
    // worker we're executing on.
    w = sf->worker;

    // Restore the pedigree chain. It must be valid because the spawn helpers
    // generated by the cilk_for implementation will access it.
    w->pedigree.parent = saved_next_pedigree_node;
}

/* capture_spawn_arg_stack_frame
 *
 * Efficiently get the address of the caller's __cilkrts_stack_frame.  The
 * preconditons are that 'w' is the worker at the time of the call and
 * 'w->current_stack_frame' points to the __cilkrts_stack_frame within the
 * spawn helper.  This function should be called only within the argument list
 * of a function that is being spawned because that is the only situation in
 * which these preconditions hold.  This function returns the worker
 * (unchanged) after storing the captured stack frame pointer is stored in the
 * sf argument.
 *
 * The purpose of this function is to get the caller's stack frame in a
 * context where the caller's worker is known but its stack frame is not
 * necessarily initialized.  The "shrink wrap" optimization delays
 * initializing the contents of a spawning function's '__cilkrts_stack_frame'
 * as well as the 'current_stack_frame' pointer within the worker.  By calling
 * this function within a spawning function's argument list, we can ensure
 * that these initializations have occured but that a detach (which would
 * invalidate the worker pointer in the caller) has not yet occured.  Once the
 * '__cilkrts_stack_frame' has been retrieved in this way, it is stable for the
 * remainder of the caller's execution, and becomes an efficient way to get
 * the worker (much more efficient than calling '__cilkrts_get_tls_worker()'),
 * even after a spawn or sync.
 */
inline __cilkrts_worker* 
capture_spawn_arg_stack_frame(__cilkrts_stack_frame* &sf, __cilkrts_worker* w)
{
    // Get current stack frame
    sf = w->current_stack_frame;
#ifdef __INTEL_COMPILER
#   if __INTEL_COMPILER <= 1300 && __INTEL_COMPILER_BUILD_DATE < 20130101
    // In older compilers 'w->current_stack_frame' points to the
    // spawn-helper's stack frame.  In newer compiler's however, it points
    // directly to the pointer's stack frame.  (This change was made to avoid
    // having the spawn helper in the frame list when evaluating function
    // arguments, thus avoiding corruption when those arguments themselves
    // contain cilk_spawns.)
    
    // w->current_stack_frame is the spawn helper's stack frame.
    // w->current_stack_frame->call_parent is the caller's stack frame.
    sf = sf->call_parent;
#   endif
#endif
    return w;
}

/*
 * cilk_for_recursive
 *
 * Templatized function to implement the recursive divide-and-conquer
 * algorithm that's how we implement a cilk_for.
 *
 * low   - Low loop index we're considering in this portion of the algorithm
 * high  - High loop index we're considering in this portion of the algorithm
 * body  - lambda function for the cilk_for loop body
 * data  - data used by the lambda function
 * grain - grain size (0 if it should be computed)
 * w     - __cilkrts_worker we're currently executing on
 * loop_root_pedigree - __cilkrts_pedigree node we generated for the root of
 *         the cilk_for loop to flatten out the internal nodes
 */
template <typename count_t, typename F>
static
void cilk_for_recursive(count_t low, count_t high,
                        F body, void *data, int grain,
                        __cilkrts_worker *w,
                        __cilkrts_pedigree *loop_root_pedigree)
{
tail_recurse:
    // Cilkscreen should not report this call in a stack trace
    // This needs to be done everytime the worker resumes
    NOTIFY_ZC_INTRINSIC((char *)"cilkscreen_hide_call", 0);

    count_t count = high - low;
    // Invariant: count > 0, grain >= 1
    if (count > grain)
    {
        // Invariant: count >= 2
        count_t mid = low + count / 2;
        // The worker is valid only until the first spawn and is expensive to
        // retrieve (using '__cilkrts_get_tls_worker') after the spawn.  The
        // '__cilkrts_stack_frame' is more stable, but isn't initialized until
        // the first spawn.  Thus, we want to grab the address of the
        // '__cilkrts_stack_frame' after it is initialized but before the
        // spawn detaches.  The only place we can do that is within the
        // argument list of the spawned function, hence the call to
        // capture_spawn_arg_stack_frame().
        __cilkrts_stack_frame *sf;
#if defined(__GNUC__) && ! defined(__INTEL_COMPILER) && ! defined(__clang__)
        // The current version of gcc initializes the sf structure eagerly.
        // We can take advantage of this fact to avoid calling
        // `capture_spawn_arg_stack_frame` when compiling with gcc.
        // Remove this if the "shrink-wrap" optimization is implemented.
        sf = w->current_stack_frame;
        _Cilk_spawn cilk_for_recursive(low, mid, body, data, grain, w,
                                       loop_root_pedigree);
#else        
        _Cilk_spawn cilk_for_recursive(low, mid, body, data, grain,
                                       capture_spawn_arg_stack_frame(sf, w),
                                       loop_root_pedigree);
#endif
        w = sf->worker;
        low = mid;

        goto tail_recurse;
    }

    // Call the cilk_for loop body lambda function passed in by the compiler to
    // execute one grain
    call_cilk_for_loop_body(low, high, body, data, w, loop_root_pedigree);
}

static void noop() { }

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
template <typename count_t, typename F>
static void cilk_for_root(F body, void *data, count_t count, int grain)
{
   /******************************IMPLEMENTATION BY MAHWISH*****************************/
    count_t delta, start, stop;
    int j, nid, num_children;
    if( ! __init_parallel ){
        __parallel_initialize();
        __init_parallel= true;
    }
    // printf("CILK-STATIC SCHEDULER-OPTIMIZED- number of threads=%d\n", nthreads);

     delta= (count+nthreads-1)/nthreads; 
     start=0;
     stop=count;


    for (int i=0; i<nthreads; i++){
        params[i].low=std::min(start+((delta)*i),stop);
        params[i].high=std::min(start+ delta*(i+1),stop);
    }

   /* first distribute across socket*/
   for (j=1,  nid=delta_socket; j<num_socket; nid+=delta_socket, j++){
      pp_exec[nid].control= true;
      pp_exec_submit( nid, body, data);
   } 
    /* now distribute within socket in a tree-like manner */
    num_children = pp_tree[MASTER].num_children;
    for(int i=0; i<num_children; i++){
       pp_exec_submit(pp_tree[MASTER].child[i],body, data);
    }

    /* now go on and do the work */
    body(data, params[MASTER].low, params[MASTER].high);

    /* wait on child nodes to finish work*/
    for(int i=0; i<num_children; i++)
       pp_exec_wait(pp_tree[MASTER].child[i]);
    for (int j=1, nid=delta_socket; j<num_socket; nid+=delta_socket, j++){
       pp_exec_wait(nid );
    }

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

CILK_ABI_THROWS_VOID __cilkrts_cilk_for_32(__cilk_abi_f32_t body, void *data,
                                            cilk32_t count, int grain)
{
    // Cilkscreen should not report this call in a stack trace
    NOTIFY_ZC_INTRINSIC((char *)"cilkscreen_hide_call", 0);
    // Check for an empty range here as an optimization - don't need to do any
    // __cilkrts_stack_frame initialization
    if (count > 0)
        cilk_for_root(body, data, count, grain);
}

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
CILK_ABI_THROWS_VOID __cilkrts_cilk_for_64(__cilk_abi_f64_t body, void *data,
                                            cilk64_t count, int grain)
{
    // Cilkscreen should not report this call in a stack trace
    NOTIFY_ZC_INTRINSIC((char *)"cilkscreen_hide_call", 0);
    // Check for an empty range here as an optimization - don't need to do any
    // __cilkrts_stack_frame initialization
    /*if (count > 0)
        cilk_for_root(body, data, count, grain);*/
}

}// end extern "C"

/* End cilk-abi-cilk-for-static.cpp */
