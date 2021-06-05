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

#include "precompiled.hpp"
#include "gc/g1/g1EpochSynchronizer.hpp"
#include "gc/g1/g1EpochUpdater.inline.hpp"
#include "gc/g1/g1CollectedHeap.inline.hpp"
#include "gc/g1/g1ConcurrentRefine.hpp"
#include "gc/g1/g1ThreadLocalData.hpp"
#include "gc/shared/suspendibleThreadSet.hpp"
#include "logging/log.hpp"
#include "runtime/atomic.hpp"
#include "runtime/handshake.hpp"
#include "runtime/safepointMechanism.inline.hpp"
#include "runtime/thread.hpp"
#include "runtime/threadSMR.inline.hpp"
#include "runtime/timerTrace.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/spinYield.hpp"

// Logging tag sequence for epoch synchronization.
#define EPOCH_TAGS gc, refine, handshake

G1EpochSynchronizer::PaddedCounter G1EpochSynchronizer::_global_epoch;
volatile uintx G1EpochSynchronizer::_global_frontier = 0;

#ifdef ASSERT
volatile size_t G1EpochSynchronizer::_pending_sync = 0;
#endif

uintx G1EpochSynchronizer::global_epoch() {
  return Atomic::load_acquire(&_global_epoch._counter);
}

G1EpochSynchronizer::G1EpochSynchronizer(bool start_sync):
  _required_frontier(start_sync ? start_synchronizing() : 0) {
}

bool G1EpochSynchronizer::frontier_happens_before(uintx f1, uintx f2) {
  // Support wrap-around due to overflow by comparing difference with max_uintx / 2.
  // Epoch counters are updated frequently, so it is safe to assume that a responsive
  // thread will never have a epoch counter that lags behind by more than max_uintx / 2.
  // Also note that if f1 == f2, this function should return false.
  return (f1 - f2) > (max_uintx / 2);
}

class G1FindMinEpochAndArmPollClosure : public ThreadClosure {
  const bool _arm_poll;
  const uintx _required_frontier;
  uintx _min_epoch;
  size_t _armed_threads;
  const bool _report_straggler;

public:
  G1FindMinEpochAndArmPollClosure(uintx required_frontier, bool arm_poll, bool report_straggler) :
    _arm_poll(arm_poll),
    _required_frontier(required_frontier),
    _min_epoch(max_uintx),
    _armed_threads(0),
    _report_straggler(report_straggler) {}

  void do_thread(Thread* thread) {
    assert(thread->is_Java_thread(), "invariant");
    uintx epoch = Atomic::load_acquire(&G1ThreadLocalData::epoch(thread));
    if (_arm_poll &&
        G1EpochSynchronizer::frontier_happens_before(epoch, _required_frontier)) {
      // The epoch counter for the current thread must have been updated already.
      assert(thread != Thread::current(), "invariant");
      JavaThread* jt = static_cast<JavaThread*>(thread);
      SafepointMechanism::arm_local_poll(jt);
      ++_armed_threads;
      {
        // Update a blocked target thread's epoch counter on behalf of the target thread.
        HandshakeState::DelegateProcessingScope dps(jt->handshake_state(), false);
        if (dps.result() == HandshakeState::ProcessResult::_processed) {
          G1EpochUpdater::update_epoch_self_or_other(thread);
          epoch = Atomic::load_acquire(&G1ThreadLocalData::epoch(thread));
        }
      }
    }
    if (G1EpochSynchronizer::frontier_happens_before(epoch, _min_epoch)) {
      _min_epoch = epoch;
    }
    if (_report_straggler &&
        G1EpochSynchronizer::frontier_happens_before(epoch, _required_frontier)) {
      log_debug(EPOCH_TAGS)(
         "%s: Target thread (%s) is still not synchronized: " UINTX_FORMAT " < " UINTX_FORMAT,
         Thread::current()->name(), thread->name(), epoch, _required_frontier);
    }
  }

  uintx min_epoch() const {
    return _min_epoch;
  }

  size_t armed_threads() const {
    return _armed_threads;
  }
};

uintx G1EpochSynchronizer::start_synchronizing() {
  assert_not_at_safepoint();
  DEBUG_ONLY(Atomic::inc(&_pending_sync);)
  // Atomic:add() provides full fence, which is required by refinement, and also
  // for epoch synchronization.
  uintx required_frontier = Atomic::add(&_global_epoch._counter, static_cast<uintx>(1));
  log_trace(EPOCH_TAGS)("%s: start_synchronizing to frontier " UINTX_FORMAT,
      Thread::current()->name(), required_frontier);
  return required_frontier;
}

void G1EpochSynchronizer::update_global_frontier(uintx latest_frontier) {
  uintx global_frontier = Atomic::load_acquire(&_global_frontier);
  if (frontier_happens_before(global_frontier, latest_frontier)) {
    Atomic::cmpxchg(&_global_frontier, global_frontier, latest_frontier);
  }
}

bool G1EpochSynchronizer::check_frontier_helper(uintx latest_frontier,
                                                uintx required_frontier) {
  if (!frontier_happens_before(latest_frontier, required_frontier)) {
    log_trace(EPOCH_TAGS)("%s: frontier synced: " UINTX_FORMAT " >= " UINTX_FORMAT,
        Thread::current()->name(), latest_frontier, required_frontier);
    update_global_frontier(latest_frontier);
    return true;
  }
  return false;
}

size_t G1EpochSynchronizer::arm_local_polls() const {
  const uintx required_frontier = _required_frontier;

  G1FindMinEpochAndArmPollClosure cl(required_frontier, true, false);
  for (JavaThreadIteratorWithHandle jtiwh; JavaThread* thread = jtiwh.next(); ) {
    cl.do_thread(thread);
  }
  size_t armed = cl.armed_threads();
  if (check_frontier_helper(cl.min_epoch(), required_frontier)) {
    assert(armed == 0, "invariant");
    return 0;
  }
  return armed;
}

bool G1EpochSynchronizer::check_synchronized_inner(bool report_straggler) const {
  assert_not_at_safepoint();

  Thread* thread = Thread::current();
  bool is_java_thread = thread->is_Java_thread();

  if (is_java_thread) {
    G1EpochUpdater::update_epoch_self(thread);
  } else {
    assert(thread->is_ConcurrentGC_thread(), "must be a refinement thread");
  }

  const uintx global_frontier = Atomic::load_acquire(&_global_frontier);
  const uintx required_frontier = _required_frontier;
  if (!frontier_happens_before(global_frontier, required_frontier)) {
    log_trace(EPOCH_TAGS)("%s: global frontier already synced: " UINTX_FORMAT " >= " UINTX_FORMAT,
        Thread::current()->name(), global_frontier, required_frontier);
    return true;
  }

  // TODO: Perhaps we need to also check for blocked threads and update their epoch here.
  // Sometimes this gets stuck for a while as check_synchronized() is called in a loop.
  G1FindMinEpochAndArmPollClosure cl(required_frontier, false, report_straggler);
  for (JavaThreadIteratorWithHandle jtiwh; JavaThread* thread = jtiwh.next(); ) {
    cl.do_thread(thread);
  }
  return check_frontier_helper(cl.min_epoch(), required_frontier);
}

bool G1EpochSynchronizer::check_synchronized(bool report_straggler) const {
  bool result = check_synchronized_inner(report_straggler);
  if(result) {
    dec_pending_sync();
  }
  return result;
}

bool G1EpochSynchronizer::synchronize() const {
  if (check_synchronized()) {
    return true;
  }

  // Issue the async handshake if not already synchronized.
  size_t threads = arm_local_polls();
  if (threads == 0) {
    dec_pending_sync();
    return true;
  }
  jlong start_timestamp = os::elapsed_counter();

  // Then repeatedly check and spin for a while.
  SpinYield yield;
  while (!check_synchronized()) {
    jlong elapsed = os::elapsed_counter() - start_timestamp;
    if (SuspendibleThreadSet::should_yield() || elapsed > _SYNCHRONIZE_WAIT_NS) {
      return false;
    } else {
      yield.wait();
    }
  }
  return true;
}

#ifdef ASSERT
void G1EpochSynchronizer::dec_pending_sync() {
  Atomic::dec(&_pending_sync);
}

void G1EpochSynchronizer::verify_during_collection_pause(size_t deferred_length) {
  assert(_pending_sync == deferred_length,
         "pending_sync(" SIZE_FORMAT ") != deferred_sync(" SIZE_FORMAT ")",
         _pending_sync, deferred_length);
  _pending_sync = 0;
}
#endif  // ASSERT
