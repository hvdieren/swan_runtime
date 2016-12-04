/* tracing.h                  -*-C++-*-
 *
 */
#ifndef INCLUDED_TRACING_DOT_H
#define INCLUDED_TRACING_DOT_H

#include <stdio.h>
#include <stdint.h>

#define WITH_TRACING 0

struct item_t {
    long tick;
    __intptr_t id0, id1;
    const char *event;
    char pad[64-sizeof(long)+2*sizeof(__intptr_t)+sizeof(const char*)];
};
#define CHUNK_SIZE 1024
struct event_tracer {
    FILE *fp;
    size_t nitems;
    size_t id;
    struct event_tracer *next;
    struct item_t items[CHUNK_SIZE];
};

#if WITH_TRACING
#define TRACER_RECORD0(w,e)      record((w)->l->event_tracer,e,0,0)
#define TRACER_RECORD1(w,e,a)    record((w)->l->event_tracer,e,(__intptr_t)(a),0)
#define TRACER_RECORD2(w,e,a,b)  record((w)->l->event_tracer,e,(__intptr_t)(a),(__intptr_t)(b))
#else
#define TRACER_RECORD0(w,e)
#define TRACER_RECORD1(w,e,a)
#define TRACER_RECORD2(w,e,a,b)
#endif

typedef struct event_tracer event_tracer_t;

event_tracer_t * create_event_tracer( const char *fname, size_t id );

void destroy_event_tracer( event_tracer_t *t );

void tracer_flush( event_tracer_t *t );

void record( event_tracer_t *tracer, const char *e, __intptr_t id0, __intptr_t id1 );

#endif // INCLUDED_TRACING_DOT_H
