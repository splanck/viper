//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/system/rt_machine_linux_cgroup.h
// Purpose: Resolve Linux cgroup controller bases from procfs mount metadata.
//
// Key invariants:
//   - Mount roots are combined with the process membership path.
//   - V1 cpu, cpuset, and memory controllers resolve independently.
//
// Ownership/Lifetime:
//   - Results are copied into caller-owned fixed buffers.
//
// Links: src/runtime/system/rt_machine.c
//
//===----------------------------------------------------------------------===//

#pragma once

enum { RT_MACHINE_CGROUP_PATH_CAPACITY = 4096 };

typedef struct rt_machine_linux_cgroup_paths {
    char unified[RT_MACHINE_CGROUP_PATH_CAPACITY];
    char cpu[RT_MACHINE_CGROUP_PATH_CAPACITY];
    char cpuset[RT_MACHINE_CGROUP_PATH_CAPACITY];
    char memory[RT_MACHINE_CGROUP_PATH_CAPACITY];
} rt_machine_linux_cgroup_paths_t;

#ifdef __cplusplus
extern "C" {
#endif

int zanna_machine_linux_resolve_cgroups(const char *mountinfo_path,
                                        const char *membership_path,
                                        rt_machine_linux_cgroup_paths_t *out);

#ifdef __cplusplus
}
#endif
