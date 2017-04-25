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
struct capture_data {
    void *body;
    void *data;
    int *work;
    int rewire;
    cilk32_t count32;
    cilk32_t delta32;
    cilk64_t count64;
    cilk64_t delta64;
    __cilkrts_hyperobject_base *hypermap;
    char *views;
    bool is32f;
};

struct static_numa_state {
    struct capture_data capture;
};

struct pp_exec_count {
    struct static_numa_state *xid;
    char padd[64-sizeof(struct static_numa_state *)];

};
typedef struct pp_exec_count pp_exec_count;

static pp_exec_count *pp_exec_c;

#define WITH_REDUCERS 0
#if WITH_REDUCERS
#define CACHE_BLOCK_SIZE 64
static __thread void *hypermap_local_view;

// Round-up view size to multiple of cache block size
#define HYPERMAP_VIEW_SIZE(r)  (((r)->__view_size + CACHE_BLOCK_SIZE-1) & ~(size_t)(CACHE_BLOCK_SIZE-1))
#define HYPERMAP_GET(r,buf,slot) (&(buf)[(slot)*HYPERMAP_VIEW_SIZE((r))])
#endif

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

static void
static_execute_capture( struct capture_data * c,
			int my_idx, int my_slot, int my_work ) {
    if( c->is32f ) {
	__cilk_abi_f32_t body32
	    = reinterpret_cast<__cilk_abi_f32_t>(c->body);
	cilk32_t idx = my_idx;
	cilk32_t work = my_work;
	cilk32_t count = c->count32;
	cilk32_t delta = c->delta32;
	cilk32_t low = std::min(delta*work,count);
	cilk32_t high = std::min(delta*(work+1),count);
	// printf( "exec32 %d (%d) work=%d count=%d %d-%d\n", my_idx, my_slot, work, count, low, high );
	// TRACER_RECORD2(__cilkrts_get_tls_worker(),"exec-capture32",low,high);
	(*body32)( c->data, low, high );
    } else {
	__cilk_abi_f64_t body64
	    = reinterpret_cast<__cilk_abi_f64_t>(c->body);
	cilk64_t idx = my_idx;
	cilk64_t work = my_work;
	cilk64_t count = c->count64;
	cilk64_t delta = c->delta64;
	cilk64_t low = std::min(delta*work,count);
	cilk64_t high = std::min(delta*(work+1),count);
	// printf( "exec64 %d (%d) work=%ld count=%ld %ld-%ld\n", my_idx, my_slot, work, count, low, high );
	// TRACER_RECORD2(__cilkrts_get_tls_worker(),"exec-capture64",low,high);
	(*body64)( c->data, low, high );
    }
}

#if WITH_REDUCERS
static void
reducer_initialize_view( __cilkrts_hyperobject_base *hypermap_reducer,
			 char *views, int slot ) {
    // If we have reducers in this loop, initialize them here
    if( hypermap_reducer ) {
	// Initialize view
	char *view = HYPERMAP_GET(hypermap_reducer,views,slot);
	(*hypermap_reducer->__c_monoid.identity_fn)(
	    (void*)&hypermap_reducer->__c_monoid, view );
	// Initialize thread-local varibale
	hypermap_local_view = view;
    }
}

static void
reduce_and_destroy_view( __cilkrts_hyperobject_base *hypermap_reducer,
			 void *left, void *right ) {
    if( hypermap_reducer ) {
	// Reduce results from other thread
	(*hypermap_reducer->__c_monoid.reduce_fn)(
	    (void*)&hypermap_reducer->__c_monoid, left, right );
	// Destroy other thread's view
	(*hypermap_reducer->__c_monoid.destroy_fn)(
	    (void*)&hypermap_reducer->__c_monoid, right );
    }
}

static void
reduce_and_destroy( __cilkrts_hyperobject_base *hypermap_reducer,
		    char *views, int left_slot, int right_slot ) {
    if( hypermap_reducer ) {
	reduce_and_destroy_view(
	    hypermap_reducer,
	    HYPERMAP_GET( hypermap_reducer, views, left_slot ),
	    HYPERMAP_GET( hypermap_reducer, views, right_slot ) );
    }
}

#endif // WITH_REDUCERS


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

    // Now we have a fork barrier between num_threads threads.
    // The lowest-numbered thread listens to the overall (inter-numa) master.
    // In the case of a user worker, that is the master.
    if( 1 /*my_idx != 0*/ ) { // 0 waits for WORKER_USER
	// Wait to start
	// int src_idx = (my_idx & 1) ? (my_idx ^ 1) : (my_idx >> 1);
	pp_exec_count *c = &pp_exec_c[my_slot];

	/* Busy wait loop for fast response */
	int counter = 128; // 300000;
        while( *((volatile void**)&c->xid) == 0) {
	    __cilkrts_yield();
	    if( --counter == 0 )
		return false; // bail out
	}
	// TRACER_RECORD2(w,"static-received0",my_idx,my_slot);
	// printf( "Worker %d (%d) received ok to run\n", my_idx, my_slot );
    }

    // This is how we get the state
    struct static_numa_state * s = pp_exec_c[my_slot].xid;

    for( int p=log_threads; p > 0; --p ) {
	int mask = (1<<p)-1;
	if( (my_idx & mask) == 0 ) {
	    // Signal other thread
	    int dst_idx = my_idx | (1<<(p-1));
	    if( dst_idx < num_threads && dst_idx != s->capture.rewire ) {
		int slot = w->g->numa_node_cum_threads[numa_node] + dst_idx;
		pp_exec_count *c = &pp_exec_c[slot];
		c->xid = s;
		// TRACER_RECORD2(w,"static-signal",dst_idx,slot);
		// printf( "Worker %d (%d) signaled %d (%d) ok to run\n", my_idx, my_slot, dst_idx, slot );
	    }
	}
    }

#if WITH_REDUCERS
    reducer_initialize_view( s->capture.hypermap, s->capture.views, my_slot );
#endif

    // Execute loop body
    // printf( "Worker %d (%d) read state %p numa=%d\n", my_idx, my_slot, s, numa_node );
    TRACER_RECORD1(w,"static-exec-start",s->capture.work[numa_node]+my_idx);
    static_execute_capture( &s->capture, my_idx, my_slot, s->capture.work[numa_node]+my_idx );
    TRACER_RECORD0(w,"static-exec-end");
    
#if WITH_REDUCERS
    hypermap_local_view = 0;
#endif

    // Wait for completion of other threads
    for( int p=log_threads; p > 0; --p ) {
	int mask = (1<<p)-1;
	if( (my_idx & mask) == 0 ) {
	    int dst_idx = my_idx | (1<<(p-1));
	    if( dst_idx < num_threads && dst_idx != s->capture.rewire ) {
		int slot = w->g->numa_node_cum_threads[numa_node] + dst_idx;
		pp_exec_count *c = &pp_exec_c[slot];

		/* Busy wait loop for fast response */
		while( *((volatile void**)&c->xid) != 0 ) {
		    __cilkrts_yield();
		}
#if WITH_REDUCERS
		reduce_and_destroy( s->capture.hypermap, s->capture.views,
				    my_slot, slot );
#endif
		// TRACER_RECORD2(w,"static-received",dst_idx,slot);
		// printf( "Worker %d (%d) received completion from %d (%d)\n",
		// my_idx, my_slot, dst_idx, slot );
	    }
	}
    }

    // Signal threads that we are done
    {
	pp_exec_count *c = &pp_exec_c[my_slot];
	c->xid = 0;
	// printf( "Worker %d (%d) signaled done\n", my_idx, my_slot );
    }

    return true;
}

void
static_scheduler_fn( __cilkrts_worker *w, int tid, signal_node_t * dyn_snode )
{
    if( w->l->type == WORKER_SYSTEM ) {
	while( 1 ) {
	    if( !static_numa_scheduler_once( w ) )
		return;
	    if( !dyn_snode || !signal_node_should_wait( dyn_snode ) )
		return;
	}
    }
}

void __parallel_initialize( global_state_t *g ) {
    size_t nbytes = sizeof(*pp_exec_c) * g->total_workers;
    pp_exec_c = (pp_exec_count *)__cilkrts_malloc( nbytes );
    memset( pp_exec_c, 0, nbytes );
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
    
    // Execute sequentially if grainsize does not allow full parallelization
    if( grain == 0 )
	grain = 1;
    int req_threads = (count + grain - 1) / grain;
    if( w->g->P == 1 || req_threads < w->g->numa_node_threads[w->l->numa_node] ) { // grain * w->g->P > count ) {
    // if( grain * w->g->P > count ) {
	TRACER_RECORD2(w,"static-start",grain,count);
	body( data, 0, count );
	TRACER_RECORD0(w,"static-end");
    } else {
	TRACER_RECORD2(w,"static-start",0,count);

	// Ensure all system workers have recorded their NUMA information.
	// Note that user workers do not record their information in the same way!
	while( w->g->numa_P_init != w->g->system_workers ) {
	    __cilkrts_yield();
	}

	// Check if we are called with NUMA restrictions in the stack frame.
	// If so, we should only use the workers on our own NUMA node. Else,
	// we can use all workers.
	bool system_wide = ( sf.flags & CILK_FRAME_NUMA ) ? false : true;

	// Determine our own NUMA node
	int master_numa_node = w->l->numa_node;

	// printf( "Cilk-static: system_wide=%d self=%d numa=%d\n", (int)system_wide, w->self, master_numa_node );

	// Allocate workers (by NUMA node)
	int numa_nodes[w->g->numa_nodes];
	int work_items[w->g->numa_nodes];
	int num_numa_nodes = 0;
	int num_threads = 0;
	if( system_wide ) {
	    for( int i=0; i < w->g->numa_nodes; ++i ) {
		// printf( "Cilk-static: status node %d/%d: %d, %d threads\n", i, w->g->numa_nodes, w->g->numa_allocate[i], w->g->numa_node_threads[i] );
		if( w->g->numa_allocate[i] != 0
		    || w->g->numa_node_threads[i] == 0 )
		    continue;
		if( __sync_bool_compare_and_swap( &w->g->numa_allocate[i], 0, 1 ) ){
		    numa_nodes[num_numa_nodes++] = i;
		    work_items[i] = num_threads;
		    num_threads += w->g->numa_node_threads[i];
		}
		if( w->l->type == WORKER_USER && w->l->numa_node == i )
		    num_threads++; // this thread
		// Only use as many threads as needed, overshooting a NUMA node
		if( num_threads >= req_threads )
		    break;
	    }
	    // TRACER_RECORD2(w,"static-alloc-systemwide",master_numa_node,num_numa_nodes);
	} else {
	    // TODO: master may be on 'wrong' NUMA node. Therefore, consider
	    //       to first try to allocate on a NUMA node as described in
	    //       the NUMA mask of the stack frame. If that fails, take the
	    //       master's NUMA node.
	    work_items[master_numa_node] = 0;
	    if( w->g->numa_allocate[master_numa_node] == 0 ) {
		if( __sync_bool_compare_and_swap(
			&w->g->numa_allocate[master_numa_node], 0, 1 ) ) {
		    numa_nodes[num_numa_nodes++] = master_numa_node;
		    num_threads += w->g->numa_node_threads[master_numa_node];
		}
	    }
	    if( w->l->type == WORKER_USER )
		num_threads++; // this thread
	    // TRACER_RECORD2(w,"static-alloc-numa",master_numa_node,num_numa_nodes);
	}
	/*
	printf( "Cilk-static: allocated %d NUMA nodes (system-wide=%d), "
		"using %d threads (%d requested) type %d\n",
		num_numa_nodes, system_wide, num_threads, req_threads,
		w->l->type );
	*/

	// Ensure (all) workers are awake. They should now be able to observe
	// that their NUMA node has been allocated and remain in the static
	// scheduler until this loop is done.
	notify_children_run(w);

    #if WITH_REDUCERS
	// Allocate space for reducer views. Allocate one view for every
	// system worker. This is the maximum required // for any invocation.
	// If there is a user worker involved, it will be the master and use the
	// original view. The array is indexed using global indices, like the
	// synchronisation array (pp_exec_c).
	// Could use class storage_for_object<> from cilk headers
	size_t view_size = hypermap ? HYPERMAP_VIEW_SIZE(hypermap) : 0;
	size_t hmap_size = w->g->system_workers * view_size;
	char hypermap_buffer[hypermap ? hmap_size + CACHE_BLOCK_SIZE:1];
	char *hypermap_views;

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
	hypermap_local_view = hypermap
	    ? (void *)(((char*)(hypermap))+(hypermap)->__view_offset)
	    : (void *)0;
    #endif

	 // If we have multiple per-NUMA schedulers, then we also need the
	 // capture to be private per scheduling domain
	 struct static_numa_state s;
	 s.capture.body = reinterpret_cast<void *>(body);
	 s.capture.data = data;
	 s.capture.is32f = is_32f<F>::value;
	 s.capture.work = work_items;
    #if WITH_REDUCERS
	 s.capture.hypermap = hypermap;
	 s.capture.views = hypermap_views;
    #endif

	 if( s.capture.is32f ) {
	     s.capture.count32 = (cilk32_t)count;
	     s.capture.delta32 = (cilk32_t)((count+num_threads-1)/num_threads);
	 } else {
	     s.capture.count64 = (cilk64_t)count;
	     s.capture.delta64 = (cilk64_t)((count+num_threads-1)/num_threads);
	 }

	 // store fence if not TSO/x86-64

	// TRACER_RECORD0(w,"static-start-distrib");

	int log_threads;

	if( w->l->type == WORKER_USER ) {
	    // No re-wiring required
	    s.capture.rewire = -1;
	    // Distribute work across sockets
	    for( int i=0; i < num_numa_nodes; ++i ) {
		// Slot for first thread on NUMA node
		pp_exec_count *c
		    = &pp_exec_c[w->g->numa_node_cum_threads[numa_nodes[i]]];
		c->xid = &s;
		// TRACER_RECORD2(w,"static-signal-worker",numa_nodes[i],w->g->numa_node_cum_threads[numa_nodes[i]]);
	    }
	} else {
	    // Distribute work intra-socket
	    log_threads = 0;
	    for( int n=num_threads-1; n != 0; n>>=1 ) { ++log_threads; }

	    int my_idx = w->l->numa_local_self;
	    s.capture.rewire = my_idx;

	    if( my_idx != 0 ) {
		int slot = w->g->numa_node_cum_threads[master_numa_node] + 0;
		pp_exec_count *c = &pp_exec_c[slot];
		c->xid = &s;
		// TRACER_RECORD2(w,"static-signal-inter",0,slot);
		// printf( "Worker %d (%d) signaled %d (%d) ok to run\n", my_idx, -1, 0, slot );
	    }

	    for( int p=log_threads; p > 0; --p ) {
		int mask = (1<<p)-1;
		if( (my_idx & mask) == 0 ) {
		    // Signal other thread
		    int dst_idx = my_idx | (1<<(p-1));
		    if( dst_idx < num_threads ) {
			int slot = w->g->numa_node_cum_threads[master_numa_node] + dst_idx;
			pp_exec_count *c = &pp_exec_c[slot];
			c->xid = &s;
			// TRACER_RECORD2(w,"static-signal-intra",dst_idx,slot);
			// printf( "Worker %d (%d) signaled %d (%d) ok to run\n", my_idx, -1, dst_idx, slot );
		    }
		}
	    }
	}

	TRACER_RECORD1(w,"static-exec-start",
		       work_items[master_numa_node]
		       + ( num_threads == 1 ? 0 : w->g->numa_node_threads[master_numa_node] ) );

	/* now go on and do the work */
	static_execute_capture( &s.capture,
				w->l->type == WORKER_USER ? num_threads-1
				: w->l->numa_local_self, -1,
				work_items[master_numa_node]
				+ ( (w->l->type == WORKER_USER) ? ( num_threads == 1 ? 0 : w->g->numa_node_threads[master_numa_node] )
				    : w->l->numa_local_self ) );

	TRACER_RECORD0(w,"static-exec-end");

	// Wait for other threads to complete
	if( w->l->type == WORKER_USER ) {
	    for( int i=0; i < num_numa_nodes; ++i ) {
		// Slot for first thread on NUMA node
		int slot = w->g->numa_node_cum_threads[numa_nodes[i]];
		pp_exec_count *c = &pp_exec_c[slot];

		/* Busy wait loop for fast response */
		while( *((volatile void**)&c->xid) != 0 ) {
		    __cilkrts_yield();
		}
    #if WITH_REDUCERS
		if( hypermap ) {
		    reduce_and_destroy_view(
			hypermap, hypermap_local_view,
			HYPERMAP_GET(hypermap, hypermap_views, slot) );
		}
    #endif
		// TRACER_RECORD2(w,"static-receive-worker",numa_nodes[i],slot);
		// printf( "thread local 0 on node %d completed\n", numa_nodes[i] );
	    }
	} else {
	    // Re-wired
	    int my_idx = w->l->numa_local_self;
	    // TODO: Reverse order of loop to get non-commutative reductions right
	    for( int p=log_threads; p > 0; --p ) {
		int mask = (1<<p)-1;
		if( (my_idx & mask) == 0 ) {
		    int dst_idx = my_idx | (1<<(p-1));
		    if( dst_idx < num_threads ) {
			int slot = w->g->numa_node_cum_threads[master_numa_node] + dst_idx;
			pp_exec_count *c = &pp_exec_c[slot];

			/* Busy wait loop for fast response */
			while( *((volatile void**)&c->xid) != 0 ) {
			    __cilkrts_yield();
			}
    #if WITH_REDUCERS
			if( hypermap ) {
			    reduce_and_destroy_view(
				hypermap, hypermap_local_view,
				HYPERMAP_GET(hypermap, hypermap_views, slot) );
			}
    #endif
			// TRACER_RECORD2(w,"static-receive-intra",dst_idx,slot);
			// printf( "Worker %d (%d) received completion from %d (%d)\n",
			// my_idx, -1, dst_idx, slot );
		    }
		}
	    }
	    if( my_idx != 0 ) {
		int slot = w->g->numa_node_cum_threads[master_numa_node] + 0;
		pp_exec_count *c = &pp_exec_c[slot];

		/* Busy wait loop for fast response */
		while( *((volatile void**)&c->xid) != 0 ) {
		    __cilkrts_yield();
		}
    #if WITH_REDUCERS
		// TODO: do this reduction the other way round - should use
		// different local view in case of re-wiring
		if( hypermap ) {
		    reduce_and_destroy_view(
			hypermap, hypermap_local_view,
			HYPERMAP_GET(hypermap, hypermap_views, slot) );
		}
    #endif
		// TRACER_RECORD2(w,"static-receive-inter",0,slot);
		// printf( "Worker %d (%d) received completion from %d (%d)\n",
		// my_idx, -1, 0, slot );
	    }
	}

	// TODO: the way we reduce results supports non-commutative
	//       reducers only in specific circumstances.
    #if WITH_REDUCERS
	// The master always uses the leftmost view.
	// The master has thus applied all updates to the final view.
	// Unset variables
	hypermap_local_view = 0;
    #endif

	// // TRACER_RECORD0(w,"leave-static-for-root");

	// De-allocate NUMA scheduling domains
	// printf( "cleaning up...\n" );
	for( int i=0; i < num_numa_nodes; ++i ) {
	    w->g->numa_allocate[numa_nodes[i]] = 0;
	}

	/* This bit is redundant as we do not spawn from within this procedure
	if( sf.flags & CILK_FRAME_UNSYNCHED ) {
	    if( !CILK_SETJMP(sf.ctx) )
		__cilkrts_sync( &sf );
	}
	*/

	// printf("CILK-STATIC SCHEDULER-OPTIMIZED- done\n" );
	TRACER_RECORD0(w,"static-end");
    }

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
