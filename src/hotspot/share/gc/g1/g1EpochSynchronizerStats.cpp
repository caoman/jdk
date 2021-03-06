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
#include "gc/g1/g1EpochSynchronizerStats.hpp"

G1EpochSynchronizerStats::G1EpochSynchronizerStats() :
    _fast_sync_time(),
    _deferred_sync_time(),
    _fast_syncs(0),
    _deferred_syncs(0) {}

G1EpochSynchronizerStats&
G1EpochSynchronizerStats::operator+=(const G1EpochSynchronizerStats& other) {
  _fast_sync_time += other._fast_sync_time;
  _deferred_sync_time += other._deferred_sync_time;
  _fast_syncs += other._fast_syncs;
  _deferred_syncs += other._deferred_syncs;
  return *this;
}

G1EpochSynchronizerStats&
G1EpochSynchronizerStats::operator-=(const G1EpochSynchronizerStats& other) {
  _fast_sync_time -= other._fast_sync_time;
  _deferred_sync_time -= other._deferred_sync_time;
  _fast_syncs -= other._fast_syncs;
  _deferred_syncs -= other._deferred_syncs;
  return *this;
}
