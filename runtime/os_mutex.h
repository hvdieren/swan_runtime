/* os_mutex.h                  -*-C++-*-
 *
 *************************************************************************
 *
 * Copyright (C) 2009-2011 , Intel Corporation
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
 * WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/

/**
 * @file os_mutex.h
 *
 * @brief Portable interface to operating-system mutexes.
 *
 * Do not confuse os_mutex with Cilk runtime-specific spinlock mutexes.
 */

#ifndef INCLUDED_OS_MUTEX_DOT_H
#define INCLUDED_OS_MUTEX_DOT_H

#include "rts-common.h"

typedef struct os_mutex os_mutex;

/**
 * __cilkrts_global_os_mutex is used to serialize accesses that may change data
 * in the global state.
 *
 * Specifically, when binding and unbinding threads, and when initializing and
 * shutting down the runtime.
 */
COMMON_SYSDEP os_mutex *__cilkrts_global_os_mutex;

/**
 * Allocate and initialize an os_mutex
 *
 * @return A pointer to the initialized os_mutex
 */
COMMON_SYSDEP os_mutex* __cilkrts_os_mutex_create(void);

/**
 * Acquire the os_mutex for exclusive use
 *
 * @param m The os_mutex that is to be acquired.
 */
COMMON_SYSDEP void __cilkrts_os_mutex_lock(os_mutex *m);

/*COMMON_SYSDEP int __cilkrts_os_mutex_trylock(os_mutex *m);*/

/**
 * Release the os_mutex
 *
 * @param m The os_mutex that is to be released.
 */
COMMON_SYSDEP void __cilkrts_os_mutex_unlock(os_mutex *m);

/**
 * Release any resources and deallocate the os_mutex.
 *
 * @param m The os_mutex that is to be deallocated.
 */
COMMON_SYSDEP void __cilkrts_os_mutex_destroy(os_mutex *m);

#endif // ! defined(INCLUDED_OS_MUTEX_DOT_H)
