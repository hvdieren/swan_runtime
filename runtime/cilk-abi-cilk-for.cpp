/* cilk-abi-cilk-for.cpp                  -*-C++-*-
 *
 *************************************************************************
 *
 * Copyright (C) 2011 , Intel Corporation
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

template <typename count_t>
static inline int grainsize(long req, count_t iter)
{
    
    if (req > 0)
    {
        // This could be if req > INT_MAX return INT_MAX but limits.h is
        // broken on some Linux's.  A limit this high risks losing
        // parallelism, but the user told us what they want for grainsize.
        // Who are we to argue?
        if (req > 0x7fffffff)
            return 0x7fffffff;
        return (int)req;
    }

    global_state_t* g = cilkg_get_global_state();
    if (g->under_ptool)
    {
        // Grainsize = 1, when running under PIN, and when the grainsize has not
        // explicitly been set by the user.
        return 1;
    }
    else
    {
        count_t n = iter / (8 * g->P) + 1;
        // 2K should be enough to amortize the cost of the cilk_for and any
        // larger grainsize risks losing parallelism
        if (n > 2048)
            return 2048;
        return (int)n;
    }
}

template <typename count_t, typename F>
static void cilk_for_recursive(count_t low, count_t high,
                               F fn, void *data, int grain)
{
  tail_recurse:
    count_t count = high - low;
    // Invariant: count > 0, grain >= 1
    if (count > grain)
    {
        // Invariant: count >= 2
        count_t mid = low + count / 2;
        _Cilk_spawn cilk_for_recursive(low, mid, fn, data, grain);
        low = mid;
        goto tail_recurse;
    }

    fn(data, low, high);
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
    if (count)
    {
        /* Spawn is necessary at top-level to force runtime to start up.
         * Runtime must be started in order to call the grainsize() function.
         */
        int gs = grainsize(grain, count);
        if (count > gs)
        {
            cilk32_t mid = count / 2;
            _Cilk_spawn cilk_for_recursive((cilk32_t) 0, mid, body, data, gs);
            cilk_for_recursive(mid, count, body, data, gs);
        }
        else
            body(data, 0, count);
    }
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
    if (count)
    {
        /* Spawn is necessary at top-level to force runtime to start up.
         * Runtime must be started in order to call the grainsize() function.
         */
        int gs = grainsize(grain, count);
        if (count > gs)
        {
            cilk64_t mid = count / 2;
            _Cilk_spawn cilk_for_recursive((cilk64_t) 0, mid, body, data, gs);
            cilk_for_recursive(mid, count, body, data, gs);
        }
        else
            body(data, 0, count);
    }
}

} // end extern "C"

/* End cilk-abi-cilk-for.cpp */
