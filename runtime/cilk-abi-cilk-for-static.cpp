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
#include "local_state.h"
#include "tracing.h"
#include <cilk/hyperobject_base.h>
#include "os.h"
#include "numa.h"

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
extern int cilk_rt_up;
typedef unsigned pp_exec_id_t;
struct pp_exec_t {
    __cilk_abi_f32_t __unused_func;
    void * __unused_arg;
    pp_exec_id_t __unused_t_id;
    //int argc;
    bool __unused_control;
    char pad[30];
};
struct capture_data {
    void *body;
    void *data;
    bool is32f;
};

struct static_numa_state {
    struct capture_data capture;
};

struct pp_exec_count{
    struct static_numa_state *xid;
    char padd[64-sizeof(struct static_numa_state *)];

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

static pp_exec_t *pp_exec;
static pp_exec_count *pp_exec_c;
static struct param *params;
static int *id;
static struct tree_struct *pp_tree;
static int nthreads;
static int num_socket;
static int delta_socket;
static int cores_per_socket;
// static struct capture_data capture;

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

static void noop(){
}

/*
 * Compiler-generated helper routines.
 * Assign distinct name to avoid triggering a bug in clang/opt related
 * to incompatible argument types on clang's internal __cilkrts_stack_frame
 * type vs. the type used in source code.
 *
 * FIXME: use capture_spawn_arg_stack_frame to update the NUMA settings
 *        on the stack frame. That would allow the use of _Cilk_spawn and
 *        avoid the replication of the routines below
 */
static void __cilkrts_inline_detach(struct __cilkrts_stack_frame *self) {
    struct __cilkrts_worker *w = self->worker;
    struct __cilkrts_stack_frame *parent = self->call_parent;
    struct __cilkrts_stack_frame *volatile *tail = w->tail;

    self->spawn_helper_pedigree.rank = w->pedigree.rank;
    self->spawn_helper_pedigree.parent = w->pedigree.parent;

    self->call_parent->parent_pedigree.rank = w->pedigree.rank;
    self->call_parent->parent_pedigree.parent = w->pedigree.parent;

    w->pedigree.rank = 0;
    w->pedigree.parent = &self->spawn_helper_pedigree;

/*assert (tail < w->ltq_limit);*/
    *tail++ = parent;

/* The stores are separated by a store fence (noop on x86)
   or the second store is a release (st8.rel on Itanium) */
    w->tail = tail;
    self->flags |= CILK_FRAME_DETACHED;
}

static void __cilkrts_inline_pop_frame(struct __cilkrts_stack_frame *sf) {
    struct __cilkrts_worker *w = sf->worker;
    w->current_stack_frame = sf->call_parent;
    sf->call_parent = 0;
}



    
/*typedef enum cilk_worker_type
{
    WORKER_FREE,    ///< Unused worker - available to be bound to user threads
    WORKER_SYSTEM,  ///< Worker created by runtime - able to steal from any worker
    WORKER_USER     ///< User thread - able to steal only from team members
}
cilk_worker_type;*/
static void notify_children(__cilkrts_worker *w, unsigned int msg)
{
    int child_num;
    __cilkrts_worker *child;
    int num_sys_workers = w->g->P - 1;

    // If worker is "n", then its children are 2n + 1, and 2n + 2.
    child_num = (w->self << 1) + 1;
    if (child_num < num_sys_workers) {
        child = w->g->workers[child_num];
        CILK_ASSERT(child->l->signal_node);
        signal_node_msg(child->l->signal_node, msg);
        child_num++;
        if (child_num < num_sys_workers) {
            child = w->g->workers[child_num];
            CILK_ASSERT(child->l->signal_node);
            signal_node_msg(child->l->signal_node, msg);
        }
    }
}
static void notify_children_run(__cilkrts_worker *w)
{
    notify_children(w, 1);
}
static bool __init_parallel=false;
//static void noop(){
//}
static bool is_up = false;
static void pp_exec_init( pp_exec_t * x, pp_exec_count *c )
{
    c->xid = 0;
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
	t->child[0]=  (nthreads-1)-(t_id-1);	/*left child id*/
	subtree= tree_size/2;	/*left subtree size*/
	pp_tree_init( &pp_tree[t->child[0]], subtree, id[t->child[0]]);
	#if 0
        t->child[0]=  t_id-1;	/*left child id*/
	subtree= tree_size/2;	/*left subtree size*/
	pp_tree_init( &pp_tree[t->child[0]], subtree, t->child[0]);
	#endif
        if(t->num_children>1){
	    t->child[1]= (nthreads-1)-(t_id-(tree_size/2)-1); /*right child id*/
	    subtree= (tree_size-1)- subtree;	/*right subtree size*/
	    pp_tree_init( &pp_tree[t->child[1]], subtree, id[t->child[1]] );
	    #if 0
    i
            t->child[1]= t_id-(tree_size/2)-1; /*right child id*/
	    subtree= (tree_size-1)- subtree;	/*right subtree size*/
	    pp_tree_init( &pp_tree[t->child[1]], subtree, t->child[1] );
            #endif
	}
    }
    #if 0 //without reversed ids//
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
    #endif
}

#if 0
static void
pp_exec_submit(int id)
{
    pp_exec_t *x= &pp_exec[id];
    pp_exec_count *c= &pp_exec_c[id];
    // xid= c->xid;
    c->xid= true;
    // assert(xid != c->xid);
}
#endif
static void asm_pause()
{
    // should help hyperthreads
    // __asm__ __volatile__( "pause" : : : );
}
#if 0
static void pp_exec_wait( int id )
{
   pp_exec_count *c= &pp_exec_c[id];
   while( c->xid==true) sched_yield();
}
#endif

static bool
static_numa_scheduler_once( __cilkrts_worker *w ) {
    assert( w->l->type == WORKER_SYSTEM );

    // Get intra-node thread index
    int numa_node = w->l->numa_node;

    // Check if NUMA node has been allocated. If not, bail out.
    if( !w->g->numa_allocate[numa_node] )
	return false;

    int my_idx = w->l->numa_local_self;
    int my_slot = w->g->numa_node_cum_threads[numa_node] + my_idx;
    // not counting user worker in thread count
    int num_threads = w->g->numa_node_threads[numa_node];

    int log_threads = 0;
    for( int n=num_threads-1; n != 0; n>>=1 ) { ++log_threads; }

    printf( "log threads=%d from %d (node %d)\n", log_threads, num_threads, numa_node );

    // Now we have a fork barrier between num_threads threads.
    // The lowest-numbered thread listens to the overall (inter-numa) master.
    // In the case of a user worker, that is the master.
    if( 1 /*my_idx != 0*/ ) { // 0 waits for WORKER_USER
	// Wait to start
	// int src_idx = (my_idx & 1) ? (my_idx ^ 1) : (my_idx >> 1);
	pp_exec_count *c = &pp_exec_c[my_slot];

	/* Busy wait loop for fast response */
        while( *((volatile void**)&c->xid) == 0) {
	    __cilkrts_yield();
	}
	printf( "Worker %d (%d) received ok to run\n", my_idx, my_slot );
    }

    // This is how we get the state
    struct static_numa_state * s = pp_exec_c[my_slot].xid;

    for( int p=log_threads; p > 0; --p ) {
	int mask = (1<<p)-1;
	if( (my_idx & mask) == 0 ) {
	    // Signal other thread
	    int dst_idx = my_idx | (1<<(p-1));
	    if( dst_idx < num_threads ) {
		int slot = w->g->numa_node_cum_threads[numa_node] + dst_idx;
		pp_exec_count *c = &pp_exec_c[slot];
		c->xid = s;
		printf( "Worker %d (%d) signaled %d (%d) ok to run\n", my_idx, my_slot, dst_idx, slot );
	    }
	}
    }

    // Execute loop body
    TRACER_RECORD0(w,"static-work");
    printf( "Worker %d (%d) read state %p numa=%d\n", my_idx, my_slot, s, numa_node );
    struct param *p= &params[my_slot]; // TODO: get offset through numa_state
    if( s->capture.is32f ) {
	__cilk_abi_f32_t body32
	    = reinterpret_cast<__cilk_abi_f32_t>(s->capture.body);
	(*body32)( s->capture.data, p->low32, p->high32 );
    } else {
	__cilk_abi_f64_t body64
	    = reinterpret_cast<__cilk_abi_f64_t>(s->capture.body);
	(*body64)( s->capture.data, p->low64, p->high64 );
    }
    
    // Wait for completion of other threads
    for( int p=log_threads; p > 0; --p ) {
	int mask = (1<<p)-1;
	if( (my_idx & mask) == 0 ) {
	    int dst_idx = my_idx | (1<<(p-1));
	    if( dst_idx < num_threads ) {
		int slot = w->g->numa_node_cum_threads[numa_node] + dst_idx;
		pp_exec_count *c = &pp_exec_c[slot];

		/* Busy wait loop for fast response */
		while( *((volatile void**)&c->xid) != 0 ) {
		    __cilkrts_yield();
		}
		printf( "Worker %d (%d) received completion from %d (%d)\n",
			my_idx, my_slot, dst_idx, slot );
	    }
	}
    }

    // Signal threads that we are done
    {
	pp_exec_count *c = &pp_exec_c[my_slot];
	c->xid = 0;
	printf( "Worker %d (%d) signaled done\n", my_idx, my_slot );
    }

    return true;
}

void
static_scheduler_fn( __cilkrts_worker *w, int tid, signal_node_t * dyn_snode )
{
    int idx= (nthreads-1)-tid;
    pp_exec_t *x = &pp_exec[idx];
    pp_exec_count *c = &pp_exec_c[idx];
    struct tree_struct *p_tree= &pp_tree[idx];
    struct param *p= &params[idx];
    /*pp_exec_t *x = &pp_exec[tid];
    pp_exec_count *c = &pp_exec_c[tid];
    struct tree_struct *p_tree= &pp_tree[tid];
    struct param *p= &params[tid];*/
    int num_children;
    int j,k;

    if( ! __init_parallel ) {
	TRACER_RECORD0(w,"static-not-init");
        return;
    }

    if( w->l->type == WORKER_SYSTEM ) {
	while( 1 ) {
	    static_numa_scheduler_once( w );
	    if( !dyn_snode || !signal_node_should_wait( dyn_snode ) )
		return;
	}
    }
}

#if 0
void
static_scheduler_fn( __cilkrts_worker *w, int tid, signal_node_t * dyn_snode )
{
    int idx= (nthreads-1)-tid;
    pp_exec_t *x = &pp_exec[idx];
    pp_exec_count *c = &pp_exec_c[idx];
    struct tree_struct *p_tree= &pp_tree[idx];
    struct param *p= &params[idx];
    /*pp_exec_t *x = &pp_exec[tid];
    pp_exec_count *c = &pp_exec_c[tid];
    struct tree_struct *p_tree= &pp_tree[tid];
    struct param *p= &params[tid];*/
    int num_children;
    int j,k;

    if( ! __init_parallel ) {
	TRACER_RECORD0(w,"static-not-init");
        return;
    }

    num_children = p_tree->num_children;
    while( 1 ) {
	/* Busy wait loop for fast response */
        while( *((volatile void**)&c->xid) == false) {
	    // We are polling Cilk's thread signal node. If it says wait,
	    // we run, if it says continue, we stop.
	    // User workers do not have signal nodes
	    if( !dyn_snode || !signal_node_should_wait( dyn_snode ) ) {
		// printf( "static sched tid=%d leaving\n", tid );
		// TRACER_RECORD0(w,"leave-static-scheduler");
		return;
	    }
	    sched_yield();
	    asm_pause();
	    // Record a steal failure and return to dynamic scheduler
            // w->l->steal_failure_count++;
	}
      /*  for(k=0; k< num_children; k++)
            printf("thread : %d , child[%d]: %d \n", tid, k, p_tree->child[k]);	*/
	// Signal other threads to get going
	for(j=0; j<num_children; j++) {
	    TRACER_RECORD1(w,"submit-child",p_tree->child[j]);
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
       TRACER_RECORD0(w,"static-work");
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
	   TRACER_RECORD1(w,"wait-child",p_tree->child[j]);
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
#if WITH_REDUCERS
       hypermap_local_view = 0; // erase
#endif
       /* Signal we're done */
       c->xid= false;
       TRACER_RECORD0(w,"static-work-done");
    } //end if while
}
#endif

void __parallel_initialize(void) {
    pthread_t tid;
    int i,j, nid, ret;

    /* Do this only once */
    if( __init_parallel )
	return;

    // TODO: with current structure, should we get the number of threads
    //       from the main cilk runtime?
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
	else if(strcmp(env_n,"bouillon")==0){
	    cores_per_socket=256;
	}
    }else{
	cores_per_socket=8; //assume hpdc 
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
    #if 0// moved the code after id init
    for (j=0, nid=0; j<num_socket; nid+=delta_socket, j++){
	int rem= nthreads-nid;
	int size= std::min(rem, delta_socket);
	pp_tree_init(&pp_tree[nid],size,nid);
    } 
    #endif
    for( i=0; i < nthreads; ++i ) {
	//id[i] =i;
	//pp_exec_c[i].xid=false;
	id[i] =(nthreads-1)-i; //mirroring ids
	pp_exec_c[i].xid=0;
    }
    for (j=0, nid=0; j<num_socket; nid+=delta_socket, j++){
    // for (j=0,  nid=nthreads-1; j<num_socket; nid-=delta_socket, j++){
	int rem= nthreads-nid;
	// int rem= nid+1;
	int size= std::min(rem, delta_socket);
	pp_tree_init(&pp_tree[nid],size,id[nid]);
    }
    int c;
    /*for(c=0; c < nthreads; c++){
      printf("\nthread indx: %d, thread id: %d, num_children=%d\n", c, id[c], pp_tree[c].num_children ); 
      for(j=0; j< pp_tree[c].num_children; j++)
        printf("child[%d] indx: %d, id: %d\n", j, pp_tree[c].child[j], id[pp_tree[c].child[j]]);
    }*/
    __init_parallel = true;
    is_up = true;
    // _Cilk_spawn noop();
    // __cilkrts_worker *w= __cilkrts_get_tls_worker();
    //CILK_ASSERT(w->g->workers[0]->l->signal_node);
    // signal_node_msg(w->g->workers[0]->l->signal_node, 1); //exclusively signal worker 0 
    // notify_children_run(w);
     //notify_children_run(w->g->workers[0]);
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
    struct __cilkrts_stack_frame sf;
    int j, nid, num_children;

    // Ensure we have a worker so that we can probe the global state
    // of the scheduler. Typically we need to enter the runtime when we call
    // this method. This also initializes the static runtime part.
    // Note that we do not want to use the trick '_Cilk_spawn noop()' because
    // that allows the current frame to be stolen while noop() executes, which
    // means we won't know for sure who is the master and who is the slave.
    __cilkrts_enter_frame_1(&sf);

    // Get the worker. Could also read it out of sf (faster).
    __cilkrts_worker * w = __cilkrts_get_tls_worker();
    
    if( ! __init_parallel )
    {
	fprintf( stderr,
		 "Really, do we still need to initialize the static runtime?\n" );
        __parallel_initialize();
    }

    // Ensure all workers have recorded their NUMA information.
    // Note that user workers do not record their information in the same way!
    while( w->g->numa_P_init != w->g->system_workers ) {
	__cilkrts_yield();
    }

    // TODO: temporary for KNL as it does not have CPUs on node 1!
    // w->g->numa_nodes = 1;

    // Check if we are called with NUMA restrictions in the stack frame.
    // If so, we should only use the workers on our own NUMA node. Else,
    // we can use all workers.
    bool system_wide = ( sf.flags & CILK_FRAME_NUMA ) ? false : true;
    printf( "Cilk-static: system_wide=%d\n", (int)system_wide );

    // Determine our own NUMA node
    int master_numa_node;
    {
	// TODO: avoid system calls
	int cpu = sched_getcpu();
	if( cpu >= 0 ) 
	    master_numa_node = numa_node_of_cpu(cpu);
	else
	    master_numa_node = 0; // always works?
    }
    
    // Allocate workers (by NUMA node)
    int numa_nodes[w->g->numa_nodes];
    int num_numa_nodes = 0;
    int num_threads = w->l->type == WORKER_USER ? 1 : 0; // this thread
    if( system_wide ) {
	for( int i=0; i < w->g->numa_nodes; ++i ) {
	    printf( "Cilk-static: status node %d/%d: %d\n", i, w->g->numa_nodes, w->g->numa_allocate[i] );
	    if( w->g->numa_allocate[i] != 0
		|| w->g->numa_node_threads[i] == 0 )
		continue;
	    if( __sync_bool_compare_and_swap( &w->g->numa_allocate[i], 0, 1 ) ){
		numa_nodes[num_numa_nodes++] = i;
		num_threads += w->g->numa_node_threads[i];
	    }
	}
    } else {
	if( w->g->numa_allocate[master_numa_node] == 0 ) {
	    if( __sync_bool_compare_and_swap(
		    &w->g->numa_allocate[master_numa_node], 0, 1 ) ) {
		numa_nodes[num_numa_nodes++] = master_numa_node;
		num_threads += w->g->numa_node_threads[master_numa_node];
	    }
	}
    }
    printf( "Cilk-static: allocated %d NUMA nodes, using %d threads\n",
	    num_numa_nodes, num_threads );

    // Ensure all workers are awake. They should now be able to observe
    // that their NUMA node has been allocated and remain in the static
    // scheduler until this loop is done.
    notify_children_run(w);

    
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
        //         HYPERMAP_VIEW_SIZE, num_threads, HYPERMAP_SIZE );
        hypermap_views = &hypermap_buffer[CACHE_BLOCK_SIZE-offset];
        assert( (uintptr_t(hypermap_views) & uintptr_t(CACHE_BLOCK_SIZE-1)) == 0 );
     } else {
        hypermap_views = NULL;
     }

     // Initialize pointer to our local view
     hypermap_local_view = hypermap_reducer ? HYPERMAP_GET(MASTER) : 0;
#endif

     // If we have multiple per-NUMA schedulers, then we also need the
     // capture to be private per scheduling domain
     struct static_numa_state s;
     s.capture.body = reinterpret_cast<void *>(body);
     s.capture.data = data;
     s.capture.is32f = is_32f<F>::value;

     // This data is per-thread and may remain shared
     if( s.capture.is32f ) {
	 count_t delta = (count+num_threads-1)/num_threads;
	 for (int i=0; i<num_threads; i++){
	     params[i].low32 = std::min((delta)*i,count);
	     params[i].high32 = std::min(delta*(i+1),count);
	 }
     } else {
	 count_t delta = (count+num_threads-1)/num_threads;
	 for (int i=0; i<num_threads; i++){
	     params[i].low64 = std::min((delta)*i,count);
	     params[i].high64 = std::min(delta*(i+1),count);
	 }
     }

     // store fence if not TSO/x86-64
   
#if 0
     // first distribute across socket
   for (j=1,  nid=nthreads-delta_socket-1; j<num_socket; nid-=delta_socket, j++){
       // TRACER_RECORD1(w,"submit-numa",nid);
       pp_exec_submit( nid );
   } 
    /* now distribute within socket in a tree-like manner */
    num_children = pp_tree[MASTER].num_children;
    for(int i=0; i<num_children; i++){
	// TRACER_RECORD1(w,"submit-mchild",pp_tree[MASTER].child[i]);
	pp_exec_submit( pp_tree[MASTER].child[i] );
    }
#endif

    // Distribute work across sockets
    for( int i=0; i < num_numa_nodes; ++i ) {
	// Slot for first thread on NUMA node
	pp_exec_count *c
	    = &pp_exec_c[w->g->numa_node_cum_threads[numa_nodes[i]]];
	c->xid = &s;
    }

    /* now go on and do the work */
    if( s.capture.is32f )
       (*body)( s.capture.data, params[MASTER].low32, params[MASTER].high32 );
    else
       (*body)( s.capture.data, params[MASTER].low64, params[MASTER].high64 );

    // Wait for other threads to complete
    for( int i=0; i < num_numa_nodes; ++i ) {
	// Slot for first thread on NUMA node
	pp_exec_count *c
	    = &pp_exec_c[w->g->numa_node_cum_threads[numa_nodes[i]]];

	/* Busy wait loop for fast response */
        while( *((volatile void**)&c->xid) != 0 ) {
	    __cilkrts_yield();
	}
	printf( "thread local 0 on node %d completed\n", numa_nodes[i] );
    }

    // TODO: the way we reduce results supports non-commutative
    //       reducers only if we get the tree right!
    //printf("going into wait\n");
#if 0
    /* wait on child nodes to finish work*/
   /* for(int l=0; l<num_children; l++){
       printf("thread master, child[%d]: %d\n", l, pp_tree[MASTER].child[l]);
    }*/
    for(int i=0; i<num_children; i++) {
        pp_exec_wait(pp_tree[MASTER].child[i]);
	// TRACER_RECORD1(w,"wait-mchild",pp_tree[MASTER].child[i]);
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
    for (j=1,  nid=nthreads-delta_socket-1; j<num_socket; nid-=delta_socket, j++){
	// Wait on tree roots on other sockets
	pp_exec_wait( nid );
	// TRACER_RECORD1(w,"wait-numa",nid);
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
    hypermap_local_view = 0;
#endif

#endif // 0

    // TRACER_RECORD0(w,"leave-static-for-root");
    // printf("CILK-STATIC SCHEDULER-OPTIMIZED- done\n" );

    // De-allocate NUMA scheduling domains
    printf( "cleaning up...\n" );
    for( int i=0; i < num_numa_nodes; ++i ) {
	w->g->numa_allocate[numa_nodes[i]] = 0;
    }

    /* This bit is redundant as we do not spawn from within this procedure
    if( sf.flags & CILK_FRAME_UNSYNCHED ) {
	if( !CILK_SETJMP(sf.ctx) )
	    __cilkrts_sync( &sf );
    }
    */

    // Cleanup and unlink stack frame
    __cilkrts_inline_pop_frame(&sf);
    if( sf.flags )
	__cilkrts_leave_frame(&sf);
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
