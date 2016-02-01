#include <stdlib.h>

#include "cilk/common.h"
#include "cilk/dataflow.h"
#include "internal/dataflow_abi.h"
#include "internal/abi.h"
#include "spin_mutex.h"

__CILKRTS_INLINE
void __cilkrts_task_list_init(__cilkrts_task_list *tl) {
    tl->tail = &tl->head;
    tl->head.st_task_and_last = NULL;
    tl->head.it_next = NULL;
}

__CILKRTS_INLINE
void __cilkrts_obj_metadata_init(__cilkrts_obj_metadata *md) {
    md->oldest_num_tasks = 0;
    md->youngest_group = CILK_OBJ_GROUP_EMPTY;
    md->num_gens = 0;
    __cilkrts_task_list_init( &md->tasks );
    spin_mutex_init( &md->mutex );
}

uint32_t __cilkrts_obj_metadata_ini_ready(__cilkrts_obj_metadata *md,
					  uint32_t grp) {
    if( md->num_gens == 1 ) {
	if( ( md->youngest_group & ((grp | CILK_OBJ_GROUP_EMPTY)
				    & CILK_OBJ_GROUP_NOT_WRITE) ) != 0 )
	    return 1;
    }
    return md->num_gens == 0;
}

void __cilkrts_obj_metadata_wakeup_hard(
    __cilkrts_ready_list *rlist, __cilkrts_obj_metadata *meta) {
    struct __cilkrts_task_list_node * head = meta->tasks.head.it_next;
    struct __cilkrts_task_list_node * i_next = 0;
    unsigned new_tasks = 0;
    for( struct __cilkrts_task_list_node * i=head; i; i=i_next ) {
	struct __cilkrts_pending_frame * t
	    = (struct __cilkrts_pending_frame *)
	    ((uintptr_t)i->st_task_and_last & ~(uintptr_t)(1));
	++new_tasks;
assert( new_tasks < 10000 ); // RM
	i_next = i->it_next;
	if( __sync_fetch_and_add( &t->incoming_count, -1 ) == 1 ) {
	    // TODO: push in front for convenience -- revise
	    // require per-worker ready list
	    t->next_ready_frame = 0;
	    rlist->tail->next_ready_frame = t;
	    rlist->tail = t;
	}
assert( t->incoming_count < 10000 ); // RM
	// if( i->is_last_in_generation() )
	if( ((uintptr_t)i->st_task_and_last & (uintptr_t)(1)) != 0 )
	    break;
    }

    // TODO: move num_tasks inc/dec outside CS and change to neg_num_tasks.
    // There may be a race on the atomic decrement on num_tasks at the beginning
    // of wakeup (if outside critical section) due to the possibility of a
    // task spawning concurrently with wakeup and completing while wakeup
    // still busy.
    // __sync_fetch_and_add( &oldest.num_tasks, new_tasks );
    // assert( oldest.num_tasks == 0 );
    meta->oldest_num_tasks = new_tasks;

    // TODO: In CS, so non-atomic dec/inc suffices?
    // pop_generation();
    meta->num_gens--;

    if( i_next ) {
	// assert( num_gens > 0 && "Zero generations but still tasks in youngest" );
	meta->tasks.head.it_next = i_next;
    } else {
	// assert( num_gens <= 1
		// && "Few generations present when depleting youngest" );
	meta->tasks.head.it_next = 0;
	meta->tasks.tail = &meta->tasks.head;
	if( meta->num_gens == 0 )
	    meta->youngest_group = CILK_OBJ_GROUP_EMPTY;
    }

    // assert( (youngest.has_tasks() == (num_gens > 0)) && "tasks require gens" );
    // printf( "%d-%p: wakeup end hard meta=%p {yg=%d, ng=%d, ont=%d} nt=%d\n", __cilkrts_get_tls_worker()->self, (void*)0, meta, meta->youngest_group, meta->num_gens, meta-> oldest_num_tasks, new_tasks );
    spin_mutex_unlock(&meta->mutex);
}

void __cilkrts_obj_metadata_wakeup(
    __cilkrts_ready_list *rlist, __cilkrts_obj_metadata *meta) {
    spin_mutex_lock( &meta->mutex );
    // printf( "%d-%p: wakeup begin meta=%p {yg=%d, ng=%d, ont=%d} \n", __cilkrts_get_tls_worker()->self, (void*)0, meta, meta->youngest_group, meta->num_gens, meta-> oldest_num_tasks );
    if( --meta->oldest_num_tasks > 0 ) {
	// printf( "%d-%p: wakeup end ont>0 meta=%p {yg=%d, ng=%d, ont=%d} \n", __cilkrts_get_tls_worker()->self, (void*)0, meta, meta->youngest_group, meta->num_gens, meta-> oldest_num_tasks );
	spin_mutex_unlock( &meta->mutex );
    } else if( meta->num_gens == 1 ) {
	meta->num_gens = 0;
	meta->youngest_group = CILK_OBJ_GROUP_EMPTY;
	// printf( "%d-%p: wakeup end ng=1 meta=%p {yg=%d, ng=%d, ont=%d}\n", __cilkrts_get_tls_worker()->self, (void*)0, meta, meta->youngest_group, meta->num_gens, meta-> oldest_num_tasks );
	spin_mutex_unlock( &meta->mutex );
    } else
	__cilkrts_obj_metadata_wakeup_hard(rlist, meta);
}


void __cilkrts_obj_metadata_add_task_read(
    __cilkrts_pending_frame *pf, __cilkrts_obj_metadata *meta,
    __cilkrts_task_list_node *tags) {
    __cilkrts_obj_metadata_add_task(pf, meta, tags, CILK_OBJ_GROUP_READ);
}

void __cilkrts_obj_metadata_add_task_write(
    __cilkrts_pending_frame *pf, __cilkrts_obj_metadata *meta,
    __cilkrts_task_list_node *tags) {
    __cilkrts_obj_metadata_add_task(pf, meta, tags, CILK_OBJ_GROUP_WRITE);
}

// TODO: specialise to add_running_task, to call in case of stealing a ready
//       (running) task and issueing it upon steal. This could be faster:
//       joins=1, pushg=0 and ready=1, so only need to set num_gens=1,
//       youngest_group=g and oldest_num_tasks++ without further ado.
//       Could it avoid the lock?
//       Alternatively, add argument 'is_running', do an if() on it and
//       hope inlining and constant propagation will do their work.
//       t==0 tells you the same...
void __cilkrts_obj_metadata_add_task(
    __cilkrts_pending_frame *t, __cilkrts_obj_metadata *meta,
    __cilkrts_task_list_node *tags, int g) {
    // Set pointer to task in argument's tags storage
    tags->st_task_and_last = t;

    // Fully mutual exclusion to avoid races
    spin_mutex_lock( &meta->mutex );

    // printf( "%d-%p: add_task begin t=%p meta=%p {yg=%d, ng=%d, ont=%d} tags=%p g=%d\n", __cilkrts_get_tls_worker()->self, (void*)0, t, meta, meta->youngest_group, meta->num_gens, meta-> oldest_num_tasks, tags, g );

    int joins = ( meta->youngest_group & ((g | CILK_OBJ_GROUP_EMPTY)
					  & CILK_OBJ_GROUP_NOT_WRITE ) ) != 0;
    int pushg = ( g
		  & ( meta->youngest_group & CILK_OBJ_GROUP_NOT_WRITE ) ) == 0;
    int ready = joins & ( meta->num_gens <= 1 );

    // push_generation( pushg );
    // TODO: in CS, so non-atomic suffices?
    // __sync_fetch_and_add( &meta->num_gens, (uint32_t)pushg );
    meta->num_gens += (uint32_t)pushg;
assert( meta->num_gens < 10000 ); // RM
    meta->oldest_num_tasks += ready;
assert( meta->oldest_num_tasks < 10000 ); // RM
    // youngest.open_group( g ); // redundant if joins == true
    meta->youngest_group = g;
    
    if( !ready ) {
	// t->add_incoming();
	__sync_fetch_and_add( &t->incoming_count, 1 );

	// We avoid branches by using a sentinel node in tasks (pointer
	// retrieved is always meaningful) and by unconditionally storing
	// a value
	__cilkrts_task_list_node * old_tail = meta->tasks.tail;
	tags->it_next = 0;
	old_tail->it_next = tags;
	// old_tail->set_last_in_generation( pushg );
	// TODO: the bit should not be set, should it? Remove "& ~1" part
	old_tail->st_task_and_last =
	    (__cilkrts_pending_frame *)
	    ( ( (uintptr_t)old_tail->st_task_and_last & ~(uintptr_t)1 )
	      | (uintptr_t)pushg );
	meta->tasks.tail = tags;
    }

    __CILKRTS_ASSERT( (meta->num_gens <= 1) == (meta->tasks.head.it_next == 0) );
    __CILKRTS_ASSERT( meta->num_gens > 0 );

    // printf( "%d-%p: add_task end t=%p meta=%p {yg=%d, ng=%d, ont=%d} tags=%p g=%d\n", __cilkrts_get_tls_worker()->self, (void*)0, t, meta, meta->youngest_group, meta->num_gens, meta-> oldest_num_tasks, tags, g );

    spin_mutex_unlock( &meta->mutex );
}


void __cilkrts_obj_metadata_add_pending_to_ready_list(
    __cilkrts_worker *w, __cilkrts_pending_frame *pf) {
    pf->next_ready_frame = 0;
    w->ready_list.tail->next_ready_frame = pf;
    w->ready_list.tail = pf;
}

void __cilkrts_move_to_ready_list(
    __cilkrts_worker *w, __cilkrts_ready_list *rlist) {
    // TODO: only makes sense if wakeup not under worker lock.
    //       also need to acquire worker lock here.
    if( 0 != rlist->head_next_ready_frame ) {
	w->ready_list.tail->next_ready_frame
	    = rlist->head_next_ready_frame;
	w->ready_list.tail = rlist->tail;
    }
}

__CILKRTS_INLINE
__cilkrts_obj_payload * __cilkrts_obj_payload_create(
    const __cilkrts_obj_traits *traits ) {
    __cilkrts_obj_payload *pl
	= (*traits->allocate_fn)( sizeof(__cilkrts_obj_payload) + traits->size );
    pl->traits = *traits;
    pl->refcnt = 1;
    pl->offset = sizeof(*pl);
    return pl;
}

/* dataflow.h
__CILKRTS_INLINE
void* __cilkrts_obj_payload_get_ptr( __cilkrts_obj_payload *pl ) {
    return (void *)(((char *)pl) + pl->offset);
}
*/

__CILKRTS_INLINE
void __cilkrts_obj_payload_destroy( __cilkrts_obj_payload *pl ) {
    (*pl->traits.destroy_fn)(__cilkrts_obj_payload_get_ptr(pl), pl);
    (*pl->traits.deallocate_fn)(__cilkrts_obj_payload_get_ptr(pl));
}

__CILKRTS_INLINE
void __cilkrts_obj_payload_add_ref( __cilkrts_obj_payload *pl ) {
    __sync_fetch_and_add( &pl->refcnt, 1 );
}

__CILKRTS_INLINE
void __cilkrts_obj_payload_del_ref( __cilkrts_obj_payload *pl ) {
    if( __sync_fetch_and_add( &pl->refcnt, -1 ) == 1 )
	__cilkrts_obj_payload_destroy( pl );
}

void __cilkrts_obj_version_init( __cilkrts_obj_version *v,
				 const __cilkrts_obj_traits *traits ) {
    __cilkrts_obj_metadata_init( &v->meta );
    v->refcnt = 1;
    v->payload = __cilkrts_obj_payload_create( traits );
    // fprintf(stderr, "%d-%p: init obj_version %p meta %p payload %p data @ %p data size: %ld\n",
    // __cilkrts_get_tls_worker() ? __cilkrts_get_tls_worker()->self : 0, (void*)0, v, &v->meta, v->payload, __cilkrts_obj_payload_get_ptr( v->payload ), traits->size );
}

// __CILKRTS_INLINE
void __cilkrts_obj_version_add_ref( __cilkrts_obj_version *v ) {
    // printf( "%d-%p: version_add_ref v=%p refcnt before=%d\n", __cilkrts_get_tls_worker()->self, (void*)0, v, v->refcnt );
    __sync_fetch_and_add( &v->refcnt, 1 );
}

/* __CILKRTS_INLINE */
void __cilkrts_obj_version_del_ref( __cilkrts_obj_version *v ) {
    // printf( "%d-%p: version_del_ref v=%p refcnt before=%d\n", __cilkrts_get_tls_worker()->self, (void*)0, v, v->refcnt );
    if( __sync_fetch_and_add( &v->refcnt, -1 ) == 1 )
	__cilkrts_obj_version_destroy( v );
}

void __cilkrts_obj_version_destroy( __cilkrts_obj_version *v ) {
    // fprintf(stderr, "%d-%p: destroy obj_version %p\n", __cilkrts_get_tls_worker()->self, (void*)0, v);
    __CILKRTS_ASSERT( v->refcnt == 0
		      && "__cilkrts_obj_version: non-zero reference count" );
    __cilkrts_obj_payload_del_ref( v->payload );
}

