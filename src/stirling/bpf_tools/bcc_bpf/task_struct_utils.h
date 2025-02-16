/*
 * Copyright 2018- The Pixie Authors.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

// LINT_C_FILE: Do not remove this line. It ensures cpplint treats this as a C file.

#pragma once

#include <linux/sched.h>

#include "src/stirling/bpf_tools/bcc_bpf/utils.h"

// This is how Linux converts nanoseconds to clock ticks.
// Used to report PID start times in clock ticks, just like /proc/<pid>/stat does.
static __inline uint64_t pl_nsec_to_clock_t(uint64_t x) {
  return div_u64(x, NSEC_PER_SEC / USER_HZ);
}

// Returns the group_leader offset.
// If GROUP_LEADER_OFFSET_OVERRIDE is defined, it is returned.
// Otherwise, the value is obtained from the definition of header structs.
// The override is important for the case when we don't have an exact header match.
// See user-space TaskStructResolver.
static __inline uint64_t task_struct_group_leader_offset() {
  if (GROUP_LEADER_OFFSET_OVERRIDE != 0) {
    return GROUP_LEADER_OFFSET_OVERRIDE;
  } else {
    return offsetof(struct task_struct, group_leader);
  }
}

// Returns the real_start_time/start_boottime offset.
// If START_BOOTTIME_OFFSET_OVERRIDE is defined, it is returned.
// Otherwise, the value is obtained from the definition of header structs.
// The override is important for the case when we don't have an exact header match.
// See user-space TaskStructResolver.
static __inline uint64_t task_struct_start_boottime_offset() {
  // Find the start_boottime of the current task.
  if (START_BOOTTIME_OFFSET_OVERRIDE != 0) {
    return START_BOOTTIME_OFFSET_OVERRIDE;
  } else {
    return offsetof(struct task_struct, START_BOOTTIME_VARNAME);
  }
}

// Effectively returns:
//   task->group_leader->start_boottime;  // Before Linux 5.5
//   task->group_leader->real_start_time; // Linux 5.5+
static __inline uint64_t get_tgid_start_time() {
  struct task_struct* task = (struct task_struct*)bpf_get_current_task();

  uint64_t group_leader_offset = task_struct_group_leader_offset();
  struct task_struct* group_leader_ptr;
  bpf_probe_read(&group_leader_ptr, sizeof(struct task_struct*),
                 (uint8_t*)task + group_leader_offset);

  uint64_t start_boottime_offset = task_struct_start_boottime_offset();
  uint64_t start_boottime = 0;
  bpf_probe_read(&start_boottime, sizeof(uint64_t),
                 (uint8_t*)group_leader_ptr + start_boottime_offset);

  return pl_nsec_to_clock_t(start_boottime);
}
