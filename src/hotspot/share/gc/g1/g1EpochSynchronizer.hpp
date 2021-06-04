/*
 * Copyright (c) 2021, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2021, Google and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_GC_G1_G1EPOCHSYNCHRONIZER_HPP
#define SHARE_GC_G1_G1EPOCHSYNCHRONIZER_HPP

#include "memory/allocation.hpp"
#include "memory/padded.hpp"

class JavaThread;

// G1EpochSynchronizer implements an epoch synchronization protocol in order to
// support asymmetric Dekker-style synchronization between all mutator threads
// and the thread doing concurrent refinement work. The epoch synchronization
// protocol guarantees that all Java heap stores in mutator threads prior to
// the initiation of the protocol are visible to the protocol-initiating thread
// when the protocol finishes. The implementation ensures that each mutator
// thread has satisfied at least one of the following conditions:
//   - the mutator thread executed an operation that implies a StoreLoad fence;
//   - the mutator thread established a release-acquire ordering with the
//     protocol-initiating thread.
//
// The implementation maintains the following data structures:
// - global_epoch: a global atomic counter;
// - T.epoch: a thread-local counter for a mutator thread T;
// - global_frontier: a minimum value of epoch counters across mutator threads;
//
// Each mutator thread copies current global_epoch to its local_epoch when
// executing certain runtime operations. For example, certain thread state
// transitions, processing a handshake. These runtime operations happen
// frequently enough to make the protocol returns quickly in most cases.
// Note that the update to T.epoch by T, and the load of T.epoch from a
// remote protocol-initiating thread also establish a release-acquire ordering.
// Thus, it is not necessary for these runtime operations to imply a StoreLoad
// fence (although they usually imply).
//
// Example usage:
//   G1EpochSynchronizer syncer(true); // starts the synchronization
//   ... // do some work that does not depend on the synchronization
//   if (syncer.synchronize()) {
//     // Synchronization successful, proceed to refinement work.
//   } else {
//     // Synchronization unsuccessful, defer or skip refinement work.
//     // An asynchronous handshake has been issued to threads that
//     // has not synchronized with the current thread.
//   }
//
// In the case of deferred refinement, the caller can use check_synchronized()
// in a loop to wait and check for the completion of the synchronization.
//
// The current implementation uses a no-op asynchronous handshake as the fallback
// approach to deal with slow synchronizations. We do not use synchronous
// handshake, because waiting for a synchronous handshake could be blocked in
// a safepoint. This blocking problem complicates refinement, especially refinemnent
// from a mutator thread's write post-barrier.
//
// In the future, we can use membarrier() syscall for OSes that support it.
// It will simplify the protocol, as synchronize() can return true after
// a membarrier() syscall, so the caller does not need to handle the
// unsuccessful synchronization case.
//
class G1EpochSynchronizer {
  // This class only contains an uintx field, so it is used as a value object.
  friend class G1FindMinEpochAndCollectThreadsClosure;

  struct PaddedCounter {
    DEFINE_PAD_MINUS_SIZE(0, DEFAULT_CACHE_LINE_SIZE, 0);
    volatile uintx _counter;
    DEFINE_PAD_MINUS_SIZE(1, DEFAULT_CACHE_LINE_SIZE, sizeof(volatile uintx));
    PaddedCounter() : _counter(0) {}
  };

  // Timeout threshold for synchronize().
  // Use a smaller threshold in debug build, in order to stress-test code paths
  // for deferred queue in G1DirtyCardQueueSet.
  static const jlong _SYNCHRONIZE_WAIT_NS = DEBUG_ONLY(3) NOT_DEBUG(3 * NANOSECS_PER_MILLISEC); // 3 millis

  // The global epoch that each Java thread will copy to its local epoch.
  static PaddedCounter _global_epoch;

  // The largest global epoch that we know all Java threads has copied.
  // _global_epoch >= _global_frontier should always be true.
  static volatile uintx _global_frontier;

  DEBUG_ONLY(static volatile size_t _pending_sync;)

  uintx _required_frontier;

  // Returns true if f1 is logically strictly smaller than f2.
  static inline bool frontier_happens_before(uintx f1, uintx f2);

  // Updates _global_frontier to MAX(_global_frontier, latest_frontier)
  static void update_global_frontier(uintx latest_frontier);

  static bool check_frontier_helper(uintx latest_frontier, uintx required_frontier);

  static uintx start_synchronizing();

  // Starts an async handshake, returns the number of threads to which the
  // async handshake were issued.
  size_t async_handshake() const;

  bool check_synchronized_inner() const;

public:
  // Load and return the global_epoch.
  static uintx global_epoch();

  // Copy constructor.
  G1EpochSynchronizer(const G1EpochSynchronizer& other) :
    _required_frontier(other._required_frontier) {}

  // If start_sync is true, start the epoch synchronization protocol.
  // Starting the synchronization provides full memory fence.
  G1EpochSynchronizer(bool start_sync);

  // Check if the synchronization has completed according to
  // the _required_frontier field.
  // Return true if it has completed, and update global frontier
  // if needed.
  bool check_synchronized() const;

  // Repeatedly check and wait for synchronization to complete,
  // according to the _required_frontier field.
  // The waiting period is bounded by the timeout threshold
  // (_SYNCHRONIZE_WAIT_NS).
  // Return true if synchronization is successful.
  // Otherwise, it reached the timeout threshold while waiting,
  // and asynchronous handshake has been issued.
  bool synchronize() const;

  static void dec_pending_sync() NOT_DEBUG_RETURN;
  DEBUG_ONLY(static void verify_during_collection_pause(size_t deferred_length);)
};

#endif // SHARE_GC_G1_G1EPOCHSYNCHRONIZER_HPP
