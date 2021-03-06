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

#ifndef SHARE_GC_G1_G1EPOCHUPDATER_INLINE_HPP
#define SHARE_GC_G1_G1EPOCHUPDATER_INLINE_HPP

#include "gc/g1/g1EpochSynchronizer.hpp"
#include "gc/g1/g1ThreadLocalData.hpp"
#include "memory/allocation.hpp"
#include "runtime/atomic.hpp"
#include "runtime/safepoint.hpp"
#include "runtime/thread.inline.hpp"

class JavaThread;

class G1EpochUpdater: public AllStatic {

  static inline void update_epoch_internal(Thread* thread) {
    // We must not update epoch inside a safepoint, in order to avoid
    // atomicity violation with resetting epoch at a safepoint.
    assert_not_at_safepoint();
    assert(thread->is_Java_thread(), "must be a Java thread");
    uintx global_epoch = G1EpochSynchronizer::global_epoch();
    volatile uintx& local_epoch = G1ThreadLocalData::epoch(thread);
    assert(Atomic::load_acquire(&local_epoch) <= global_epoch, "Epoch overflow");
    Atomic::release_store(&local_epoch, global_epoch);
  }

public:
  static inline void update_epoch_self(Thread* thread) {
    assert(thread == Thread::current(), "epoch is updated by a remote thread");
    update_epoch_internal(thread);
  }

  static inline void update_epoch_self_or_other(Thread* thread) {
    update_epoch_internal(thread);
  }
};

#endif // SHARE_GC_G1_G1EPOCHUPDATER_INLINE_HPP
