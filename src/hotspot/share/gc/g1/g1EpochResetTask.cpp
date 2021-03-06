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
#include "gc/g1/g1CollectedHeap.inline.hpp"
#include "gc/g1/g1EpochResetTask.hpp"
#include "gc/g1/g1EpochSynchronizer.hpp"
#include "runtime/vmThread.hpp"

G1EpochResetTask* G1EpochResetTask::_instance = NULL;

class VM_G1ResetEpoch: public VM_Operation {
public:
  VM_G1ResetEpoch() {}
  virtual VMOp_Type type() const { return VMOp_G1ResetEpoch; }
  virtual void doit() {
    G1EpochSynchronizer::reset_all_epoch();
  }
};

G1EpochResetTask::G1EpochResetTask() : G1ServiceTask("G1 Epoch Reset Task") {}

void G1EpochResetTask::initialize() {
  if (!G1TestEpochSyncInConcRefinement) {
    return;
  }
  assert(_instance == NULL, "Already initialized");
  _instance = new G1EpochResetTask();
  G1CollectedHeap::heap()->service_thread()->register_task(_instance);
}

void G1EpochResetTask::execute() {
  VM_G1ResetEpoch reset_epoch;
  VMThread::execute(&reset_epoch);
}

void G1EpochResetTask::schedule() {
  G1CollectedHeap::heap()->service_thread()->schedule_task(_instance, 0);
}
