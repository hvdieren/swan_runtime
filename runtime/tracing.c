#include <internal/abi.h>
#include <cilk/cilk_api.h>

#include "tracing.h"
#include "local_state.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>

static struct timeval global_start_time;
static int initialized = 0;

event_tracer_t *
create_event_tracer( const char *fname, size_t id ) {
    // Record start time once
    if( __sync_bool_compare_and_swap( &initialized, 0, 1 ) )
	gettimeofday( &global_start_time, 0 );

    event_tracer_t *t = malloc(sizeof(event_tracer_t));
    if( !(t->fp = fopen( fname, "w" )) ) {
	fprintf( stderr, "Error opening %s: %s\n", fname, strerror(errno) );
	exit(1);
    }
    t->nitems = 0;
    t->id = id;
    return t;
}

void tracer_flush( event_tracer_t *t ) {
    struct timeval start, end;
    size_t i;

    gettimeofday( &start, 0 );

    for( i=0; i < t->nitems; ++i )
	fprintf( t->fp, "%ld %ld %s %lx %lx\n",
		 t->items[i].tick, t->id, t->items[i].event,
		 t->items[i].id0, t->items[i].id1 );

    gettimeofday( &end, 0 );

    long delay
	= (long)(((double)(end.tv_usec)+(double)(end.tv_sec)*1000000.0)
		 -((double)(start.tv_usec) +(double)(start.tv_sec)*1000000.0));
    long tick
	= (long)(((double)(end.tv_usec)+(double)(end.tv_sec)*1000000.0)
		 -((double)(global_start_time.tv_usec)
		   +(double)(global_start_time.tv_sec)*1000000.0));

    fprintf( t->fp, "%ld %ld %s %lx %lx\n", tick, t->id, "flush", (long)t->nitems, delay );
    t->nitems = 0;
}

void
destroy_event_tracer( event_tracer_t *t ) {
    tracer_flush(t);
    fclose(t->fp);
    free(t);
}

void record( event_tracer_t *tracer, const char *e, __intptr_t id0, __intptr_t id1 ) {
    if( tracer->nitems >= TRACER_CHUNK_SIZE ) {
	__cilkrts_worker *w = __cilkrts_get_tls_worker();
	assert( w->l->event_tracer == tracer ); // only we record!
	tracer_flush( tracer );
    }

    struct timeval now;
    gettimeofday( &now, 0 );
    long tick
	= (long)(((double)(now.tv_usec)+(double)(now.tv_sec)*1000000.0)
		 -((double)(global_start_time.tv_usec)
		   +(double)(global_start_time.tv_sec)*1000000.0));
    tracer->items[tracer->nitems].tick = tick;
    tracer->items[tracer->nitems].event = e;
    tracer->items[tracer->nitems].id0 = id0;
    tracer->items[tracer->nitems].id1 = id1;
    tracer->nitems++;
}
