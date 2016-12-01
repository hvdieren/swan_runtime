#include "tracing.h"
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
    size_t i;
    for( i=0; i < t->nitems; ++i )
	fprintf( t->fp, "%ld %ld %s %lx %lx\n",
		 t->items[i].tick, t->id, t->items[i].event,
		 t->items[i].id0, t->items[i].id1 );
    t->nitems = 0;
    fflush( t->fp );
}

void
destroy_event_tracer( event_tracer_t *t ) {
    tracer_flush(t);
    fclose(t->fp);
    free(t);
}

void record( event_tracer_t *tracer, const char *e, __intptr_t id0, __intptr_t id1 ) {
    if( tracer->nitems >= CHUNK_SIZE )
	tracer_flush( tracer );

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
