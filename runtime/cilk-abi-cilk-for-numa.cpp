/* cilk-abi-cilk-for.cpp                  -*-C++-*-
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

#include "cilk/dataflow.h"
#include "internal/abi.h"
#include "internal/dataflow_abi.h"
#include "metacall_impl.h"
#include "global_state.h"
#include "local_state.h"
#include <stdio.h> // TODO: remove
#include <assert.h> // TODO: remove

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

extern "C" {

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
void __cilkrts_inline_detach(struct __cilkrts_stack_frame *self) {
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

void __cilkrts_inline_pop_frame(struct __cilkrts_stack_frame *sf) {
    struct __cilkrts_worker *w = sf->worker;
    w->current_stack_frame = sf->call_parent;
    sf->call_parent = 0;
}


}

#if 0
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
#endif

/*
 * Pre-declaration
 */
template <typename count_t, typename F>
static
void cilk_for_numa_recursive_spawn_helper(count_t low, count_t high,
					  F body, void *data, int grain,
					  __cilkrts_worker *w,
					  __cilkrts_pedigree *loop_root_pedigree)
    __attribute__((noinline));

/*
 * cilk_for_numa_recursive
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
void cilk_for_numa_recursive(count_t low, count_t high,
			     F body, void *data, int grain,
			     __cilkrts_worker *w,
			     __cilkrts_pedigree *loop_root_pedigree)
{
    struct __cilkrts_stack_frame cc_sf;
    __cilkrts_enter_frame_1(&cc_sf);
    /* What if NUMA flags have already been set on the parent frame?
     * Do we override them? Possibly we can make this configurable by
     * the programmer. For now we simply override. */
    cc_sf.flags |= CILK_FRAME_NUMA;
    cc_sf.numa_low = low;
    cc_sf.numa_high = high;

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
	// Determine to traverse low/high sub-range first depending on
	// numa mask and NUMA ID of worker.
	count_t numa_w = __cilkrts_get_worker_numa(w);

	// printf( "w=%d w_numa=%d low=%d mid=%d high=%d w=%p\n",
	// (int)w->self, (int)numa_w, (int)low, (int)mid, (int)high, w );

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
	/*
        _Cilk_spawn cilk_for_recursive(low, mid, numa, body, data, grain, w,
                                       loop_root_pedigree);
	*/
	if( !CILK_SETJMP(cc_sf.ctx) )
	    cilk_for_numa_recursive_spawn_helper(low, mid, body, data,
						 grain, w, loop_root_pedigree);
#else        
	/*
        _Cilk_spawn cilk_for_recursive(low, mid, body, data, grain,
                                       capture_spawn_arg_stack_frame(sf, w),
                                       loop_root_pedigree);
	*/
	// Save floating-point state
	asm( "stmxcsr %0\n\t"
	     "fnstcw %1" : "=m"(cc_sf.mxcsr), "=m"(cc_sf.fpcsr)
	     : : );
	if( !CILK_SETJMP(cc_sf.ctx) ) {
	    if( numa_w < mid ) {
		/* Adjust NUMA range on current stack frame to remainder */
		// printf( "change NUMA range %d-%d to %d-%d\n",
			// (int)cc_sf.numa_low, (int)cc_sf.numa_high,
			// (int)mid, (int)cc_sf.numa_high );
		cc_sf.numa_low = mid;
		cilk_for_numa_recursive_spawn_helper(
		    low, mid, body, data, grain,
		    capture_spawn_arg_stack_frame(sf, w),
		    loop_root_pedigree);
	    } else {
		/* Adjust NUMA range on current stack frame to remainder */
		// printf( "change NUMA range %d-%d to %d-%d\n",
			// (int)cc_sf.numa_low, (int)cc_sf.numa_high,
			// (int)cc_sf.numa_low, (int)mid );
		cc_sf.numa_high = mid;
		cilk_for_numa_recursive_spawn_helper(
		    mid, high, body, data, grain,
		    capture_spawn_arg_stack_frame(sf, w),
		    loop_root_pedigree);
	    }
	}
#endif
        w = sf->worker;
	if( numa_w < mid ) {
	    low = mid;
	} else {
	    high = mid;
	}

        goto tail_recurse;
    }

    // Call the cilk_for loop body lambda function passed in by the compiler to
    // execute one grain
    // printf( "to body %d-%d me=%d w=%p\n", (int)low, (int)high, __cilkrts_get_worker_numa(w), w );
    call_cilk_for_loop_body(low, high, body, data, w, loop_root_pedigree);

    if( cc_sf.flags & CILK_FRAME_UNSYNCHED ) {
	if( !CILK_SETJMP(cc_sf.ctx) )
	    __cilkrts_sync( &cc_sf );
    }

    __cilkrts_inline_pop_frame(&cc_sf);
    if( cc_sf.flags )
	__cilkrts_leave_frame(&cc_sf);
}

/*
 * cilk_for_numa_recursive_spawn_helper
 *
 * Manual outline of spawn helper function for cilk_for_numa_recursive.
 *
 */
template <typename count_t, typename F>
void cilk_for_numa_recursive_spawn_helper(count_t low, count_t high,
					  F body, void *data, int grain,
					  __cilkrts_worker *w,
					  __cilkrts_pedigree *loop_root_pedigree)
{
    __cilkrts_stack_frame sf;
    __cilkrts_enter_frame_fast(&sf);
    __cilkrts_inline_detach(&sf);
    sf.flags |= CILK_FRAME_NUMA; // should be narrowing? _detach_numa(&sf,lo,hi)
    sf.numa_low = low;
    sf.numa_high = high;

    // printf( "help %d-%d me=%d %p\n", (int)low, (int)high, __cilkrts_get_worker_numa(sf.worker), sf.worker );
    cilk_for_numa_recursive<count_t,F>(low, high, body, data, grain,
				       w, loop_root_pedigree);

    __cilkrts_inline_pop_frame(&sf);
    if(sf.flags)
	__cilkrts_leave_frame(&sf);
}

#if 0
static void noop() { }
#endif

/*
 * cilk_for_numa_root
 *
 * Templatized function to implement the top level of a cilk_for loop.
 *
 * body     - lambda function for the cilk_for loop body
 * data     - data used by the lambda function
 * count    - trip count for loop
 * grain    - grain size (0 if it should be computed)
 */
template <typename count_t, typename F>
static void cilk_for_numa_root_recursive(F body, void *data, count_t count, int grain)
{
    // Cilkscreen should not report this call in a stack trace
    NOTIFY_ZC_INTRINSIC((char *)"cilkscreen_hide_call", 0);

    // Pedigree computation:
    //
    //    If the last pedigree node on entry to the _Cilk_for has value X,
    //    then at the start of each iteration of the loop body, the value of
    //    the last pedigree node should be 0, the value of the second-to-last
    //    node should equal the loop counter, and the value of the
    //    third-to-last node should be X.  On return from the _Cilk_for, the
    //    value of the last pedigree should be incremented to X+2. The
    //    pedigree within the loop is thus flattened, such that the depth of
    //    recursion does not affect the results either inside or outside of
    //    the loop.  Note that the pedigree after the loop exists is the same
    //    as if a single spawn and sync were executed within this function.

    // TBD: Since the shrink-wrap optimization was turned on in the compiler,
    // it is not possible to get the current stack frame without actually
    // forcing a call to bind-thread.  This spurious spawn is a temporary
    // stopgap until the correct intrinsics are added to give us total control
    // over frame initialization.
    _Cilk_spawn noop();

    // Fetch the current worker.  From that we can get the current stack frame
    // which will be constant even if we're stolen
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    __cilkrts_stack_frame *sf = w->current_stack_frame;

    // Decrement the rank by one to undo the pedigree change from the
    // _Cilk_spawn
    --w->pedigree.rank;

    // Save the current worker pedigree into loop_root_pedigree, which will be
    // the root node for our flattened pedigree.
    __cilkrts_pedigree loop_root_pedigree = w->pedigree;

    // Don't splice the loop_root node in yet.  It will be done when we
    // call the loop body lambda function
//    w->pedigree.rank = 0;
//    w->pedigree.next = &loop_root_pedigree;

    /* Spawn is necessary at top-level to force runtime to start up.
     * TBD: Runtime must be started in order to call the grainsize() function.
     */
    int gs = grainsize(grain, count);
    cilk_for_numa_recursive((count_t) 0, count, body, data,
			    gs, w, &loop_root_pedigree);

    // Need to refetch the worker after calling a spawning function.
    w = sf->worker;

    // Restore the pedigree in the worker.
    w->pedigree = loop_root_pedigree;

    // Bump the worker pedigree.
    ++w->pedigree.rank;

    // Implicit sync will increment the pedigree leaf rank again, for a total
    // of two increments.  If the noop spawn above is removed, then we'll need
    // to re-enable the following code:
//     // If this is an optimized build, then the compiler will have optimized
//     // out the increment of the worker's pedigree in the implied sync.  We
//     // need to add one to make the pedigree_loop test work correctly.
// #if CILKRTS_OPTIMIZED
//     ++sf->worker->pedigree.rank;
// #endif
}

template <typename count_t, typename F>
struct __cilkrts_numa_args_tags {
    struct {
	count_t low;
	count_t high;
	F body;
	void * data;
	__cilkrts_pedigree *loop_root_pedigree;
	int numa_node;
    } args;
    struct {
	__cilkrts_task_list_node x;
    } tags;
};

template <typename count_t, typename F>
static void numa_call_fn( struct __cilkrts_stack_frame * sf ) {
    struct __cilkrts_numa_args_tags<count_t,F> * at
	= (struct __cilkrts_numa_args_tags<count_t,F> *)sf->args_tags;
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    // printf( "call_fn for worker %d iter %d on numa %d\n", w->self, at->args.low, w->l->numa_node );
    assert( w->l->numa_node == at->args.low );
    call_cilk_for_loop_body(at->args.low, at->args.high, at->args.body,
			    at->args.data, w, at->args.loop_root_pedigree);
    // Release is a no-op
    // Re-read worker just in case the called function contained spawns
    // w = __cilkrts_get_tls_worker();
    if( sf->flags & CILK_FRAME_UNSYNCHED ) {
	if( !CILK_SETJMP(sf->ctx) )
	    __cilkrts_sync( sf );
    }

    __cilkrts_inline_pop_frame(sf);
    if( sf->flags )
	__cilkrts_leave_frame(sf);
}

template <typename count_t, typename F>
static void cilk_for_numa_root(F body, void *data, count_t count, int grain)
{
    // Cilkscreen should not report this call in a stack trace
    NOTIFY_ZC_INTRINSIC((char *)"cilkscreen_hide_call", 0);

    // Pedigree computation:
    //
    //    If the last pedigree node on entry to the _Cilk_for has value X,
    //    then at the start of each iteration of the loop body, the value of
    //    the last pedigree node should be 0, the value of the second-to-last
    //    node should equal the loop counter, and the value of the
    //    third-to-last node should be X.  On return from the _Cilk_for, the
    //    value of the last pedigree should be incremented to X+2. The
    //    pedigree within the loop is thus flattened, such that the depth of
    //    recursion does not affect the results either inside or outside of
    //    the loop.  Note that the pedigree after the loop exists is the same
    //    as if a single spawn and sync were executed within this function.

    // TBD: Since the shrink-wrap optimization was turned on in the compiler,
    // it is not possible to get the current stack frame without actually
    // forcing a call to bind-thread.  This spurious spawn is a temporary
    // stopgap until the correct intrinsics are added to give us total control
    // over frame initialization.
    _Cilk_spawn noop();

    // Fetch the current worker.  From that we can get the current stack frame
    // which will be constant even if we're stolen
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    __cilkrts_stack_frame *sf = w->current_stack_frame;

    // printf( "for_root_numa worker %d\n", w->self );

    // Decrement the rank by one to undo the pedigree change from the
    // _Cilk_spawn
    --w->pedigree.rank;

    // Save the current worker pedigree into loop_root_pedigree, which will be
    // the root node for our flattened pedigree.
    __cilkrts_pedigree loop_root_pedigree = w->pedigree;

    // Don't splice the loop_root node in yet.  It will be done when we
    // call the loop body lambda function
//    w->pedigree.rank = 0;
//    w->pedigree.next = &loop_root_pedigree;

    /* Spawn is necessary at top-level to force runtime to start up.
     * TBD: Runtime must be started in order to call the grainsize() function.
     */
    int gs = grainsize(grain, count);
/*
    cilk_for_numa_recursive((count_t) 0, count, body, data,
			    gs, w, &loop_root_pedigree);
*/
    // For each of node=0..count-1, issue a pending
    // task on the appropriate NUMA domain, unless if it is ours, then we
    // execute.
    for( count_t node=0; node < count; ++node ) {
	if( w->l->numa_node == (int)node ) { // our work
	    // printf( "worker %d on numa %d performs iteration %d\n", 
	    // w->self, w->l->numa_node, node );
	    // Make current stack frame stealable
	    // Error: node is given incorrect value. This appears to be an
	    // error in the capture, which should be by value. Circumvent by
	    // using the constant stored in w->l->numa_node.
	    int n = w->l->numa_node;
	    _Cilk_spawn cilk_for_numa_recursive(n, n+1, body, data,
						1, w, &loop_root_pedigree);
	    // Need to refetch the worker after calling a spawning function.
	    w = sf->worker;
	} else { // create pending frame
	    // printf( "worker %d on numa %d makes iteration %d pending\n", 
	    // w->self, w->l->numa_node, node );
	    // Create pending frame and associated stack frame
	    __cilkrts_pending_frame *pf
		= __cilkrts_pending_frame_create(sizeof(__cilkrts_numa_args_tags<count_t,F>));

	    // Initialize pending frame
	    __cilkrts_numa_args_tags<count_t,F> * at =
		(__cilkrts_numa_args_tags<count_t,F> *)pf->args_tags;
	    at->args.low = node;
	    at->args.high = node+1;
	    at->args.body = body;
	    at->args.data = data;
	    at->args.loop_root_pedigree = &loop_root_pedigree;
	    at->args.numa_node = node;

	    // Set the team for the task
	    pf->team = w->l->team;
	    
	    // set NUMA flags in stack frame associated to pending frame
	    pf->numa_low = node;
	    pf->numa_high = node+1;

	    pf->call_fn = &numa_call_fn<count_t,F>;

	    // Detach pending frame
	    __cilkrts_detach_pending( pf );

	    // Make pending frame ready to execute (issue)
	    if( __sync_fetch_and_add( &pf->incoming_count, -1 ) == 1 )
		// TODO: replace by worker on desired numa_node
		// ? Create w->g->numa_ready_list[node]?
		for( int i=0; i < w->g->system_workers; ++i ) {
		    if( w->g->workers[i]->l->numa_node == node ) {
			// printf( "publish iteration %d on worker %d with numa %d\n", node, i, w->g->workers[i]->l->numa_node );
			__cilkrts_obj_metadata_add_pending_to_ready_list(
			    w->g->workers[i], pf );
			break;
		    }
		}
	}
    }
    _Cilk_sync;

    // Need to refetch the worker after calling a spawning function.
    w = sf->worker;

    // Restore the pedigree in the worker.
    w->pedigree = loop_root_pedigree;

    // Bump the worker pedigree.
    ++w->pedigree.rank;

    // Implicit sync will increment the pedigree leaf rank again, for a total
    // of two increments.  If the noop spawn above is removed, then we'll need
    // to re-enable the following code:
//     // If this is an optimized build, then the compiler will have optimized
//     // out the increment of the worker's pedigree in the implied sync.  We
//     // need to add one to make the pedigree_loop test work correctly.
// #if CILKRTS_OPTIMIZED
//     ++sf->worker->pedigree.rank;
// #endif
}

// Use extern "C" to suppress name mangling of __cilkrts_cilk_for_32 and
// __cilkrts_cilk_for_64.
extern "C" {

/*
 * __cilkrts_cilk_for_numa_32
 *
 * Implementation of cilk_for for 32-bit trip counts (regardless of processor
 * word size).  Assumes that the range is 0 - count.
 *
 * body  - lambda function for the cilk_for loop body
 * data  - data used by the lambda function
 * count - trip count for loop
 * grain - grain size (0 if it should be computed)
 */

// TODO: list in symbols.ver under ABI2.0

CILK_ABI_THROWS_VOID
__cilkrts_cilk_for_numa_32(__cilk_abi_f32_t body, void *data,
			   cilk32_t count, int grain)
{
    // Cilkscreen should not report this call in a stack trace
    NOTIFY_ZC_INTRINSIC((char *)"cilkscreen_hide_call", 0);

    // Check for an empty range here as an optimization - don't need to do any
    // __cilkrts_stack_frame initialization
    if (count > 0)
        cilk_for_numa_root(body, data, count, grain);
}

/*
 * __cilkrts_cilk_for_numa_64
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
__cilkrts_cilk_for_numa_64(__cilk_abi_f64_t body, void *data,
			   cilk64_t count, int grain)
{
    // Cilkscreen should not report this call in a stack trace
    NOTIFY_ZC_INTRINSIC((char *)"cilkscreen_hide_call", 0);

    // Check for an empty range here as an optimization - don't need to do any
    // __cilkrts_stack_frame initialization
    if (count > 0)
        cilk_for_numa_root(body, data, count, grain);
}

} // end extern "C"

/* End cilk-abi-cilk-for.cpp */
