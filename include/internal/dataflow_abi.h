/*  dataflow.h                 -*- C++ -*-
 *
 *  Copyright (C) 2015, Hans Vandierendonck, Queen's University Belfast
 *  All rights reserved.
 */
#ifndef DATAFLOW_ABI_H_INCLUDED
#define DATAFLOW_ABI_H_INCLUDED

// Includes stdint.h
#include "cilk/common.h"
#include "internal/abi.h"
#include "cilk/../../runtime/spin_mutex.h"

__CILKRTS_BEGIN_EXTERN_C

void __cilkrts_obj_metadata_wakeup(
    __cilkrts_ready_list *, __cilkrts_obj_metadata * meta);
void __cilkrts_obj_metadata_wakeup_hard(
    __cilkrts_ready_list *, __cilkrts_obj_metadata * meta);
// __cilkrts_pending_frame *__cilkrts_pending_frame_create(uint32_t);

void __cilkrts_obj_metadata_add_pending_to_ready_list(
    __cilkrts_worker *, __cilkrts_pending_frame *);
void __cilkrts_move_to_ready_list(
    __cilkrts_worker *, __cilkrts_ready_list *);

__CILKRTS_END_EXTERN_C

#endif // DATAFLOW_ABI_H_INCLUDED
