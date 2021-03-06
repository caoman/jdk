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

#ifndef SHARE_GC_G1_G1EPOCHSYNCHRONIZERSTATS_HPP
#define SHARE_GC_G1_G1EPOCHSYNCHRONIZERSTATS_HPP

#include "memory/allocation.hpp"
#include "utilities/ticks.hpp"

// Statistic counters for G1EpochSynchronizer.
class G1EpochSynchronizerStats : public StackObj {
  Tickspan _fast_sync_time;
  Tickspan _deferred_sync_time;
  size_t _fast_syncs;
  size_t _deferred_syncs;

public:
  G1EpochSynchronizerStats();

  Tickspan fast_sync_time() const { return _fast_sync_time; }
  Tickspan deferred_sync_time() const { return _deferred_sync_time; }

  size_t fast_syncs() const { return _fast_syncs; }
  size_t deferred_syncs() const { return _deferred_syncs; }

  void inc_fast_sync_time(Tickspan t) { _fast_sync_time += t; }
  void inc_deferred_sync_time(Tickspan t) { _deferred_sync_time += t; }
  void inc_fast_syncs() { ++_fast_syncs; }
  void inc_deferred_syncs() { ++_deferred_syncs; }

  G1EpochSynchronizerStats& operator+=(const G1EpochSynchronizerStats& other);
  G1EpochSynchronizerStats& operator-=(const G1EpochSynchronizerStats& other);

  friend G1EpochSynchronizerStats operator+(G1EpochSynchronizerStats x,
                                           const G1EpochSynchronizerStats& y) {
    return x += y;
  }

  friend G1EpochSynchronizerStats operator-(G1EpochSynchronizerStats x,
                                           const G1EpochSynchronizerStats& y) {
    return x -= y;
  }
};

#endif // SHARE_GC_G1_G1EPOCHSYNCHRONIZERSTATS_HPP
