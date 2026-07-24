//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/system/rt_machine_linux_cgroup.c
// Purpose: Resolve active Linux cgroup v1/v2 directories for the current process.
//
// Key invariants:
//   - Procfs octal path escapes are decoded before path composition.
//   - A membership must reside under the mount's advertised root.
//   - Malformed and overlong records are ignored without partial publication.
//
// Ownership/Lifetime:
//   - Opens and closes both input files per resolution; retains no global state.
//
// Links: src/runtime/system/rt_machine_linux_cgroup.h
//
//===----------------------------------------------------------------------===//

#include "rt_machine_linux_cgroup.h"

#include "rt_platform.h"

#include <stdio.h>
#include <string.h>

#if RT_PLATFORM_LINUX

typedef struct rt_machine_cgroup_memberships {
    char unified[RT_MACHINE_CGROUP_PATH_CAPACITY];
    char cpu[RT_MACHINE_CGROUP_PATH_CAPACITY];
    char cpuset[RT_MACHINE_CGROUP_PATH_CAPACITY];
    char memory[RT_MACHINE_CGROUP_PATH_CAPACITY];
} rt_machine_cgroup_memberships_t;

static int cgroup_copy(char *out, size_t capacity, const char *value) {
    size_t length = value ? strlen(value) : 0;
    if (!out || capacity == 0 || !value || length >= capacity)
        return 0;
    memcpy(out, value, length + 1u);
    return 1;
}

static int cgroup_has_token(const char *list, const char *requested) {
    size_t requested_length = strlen(requested);
    const char *cursor = list;
    while (cursor && *cursor) {
        const char *end = strchr(cursor, ',');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (length == requested_length && memcmp(cursor, requested, length) == 0)
            return 1;
        cursor = end ? end + 1 : NULL;
    }
    return 0;
}

static int cgroup_decode_mount_path(const char *encoded, char *out, size_t capacity) {
    size_t used = 0;
    for (size_t index = 0; encoded && encoded[index] != '\0'; ++index) {
        unsigned char value = (unsigned char)encoded[index];
        if (value == '\\' && encoded[index + 1] >= '0' && encoded[index + 1] <= '7' &&
            encoded[index + 2] >= '0' && encoded[index + 2] <= '7' &&
            encoded[index + 3] >= '0' && encoded[index + 3] <= '7') {
            value = (unsigned char)(((encoded[index + 1] - '0') << 6u) |
                                    ((encoded[index + 2] - '0') << 3u) |
                                    (encoded[index + 3] - '0'));
            index += 3;
            if (value == '\0')
                return 0;
        }
        if (used + 1u >= capacity)
            return 0;
        out[used++] = (char)value;
    }
    if (used == 0 || out[0] != '/')
        return 0;
    out[used] = '\0';
    return 1;
}

static int cgroup_join_membership(const char *mount_root,
                                  const char *mount_point,
                                  const char *membership,
                                  char *out,
                                  size_t capacity) {
    size_t root_length = strlen(mount_root);
    const char *suffix = membership;
    if (strcmp(mount_root, "/") != 0) {
        if (strncmp(membership, mount_root, root_length) != 0 ||
            (membership[root_length] != '\0' && membership[root_length] != '/'))
            return 0;
        suffix = membership + root_length;
    }
    size_t mount_length = strlen(mount_point);
    if (mount_length > 0 && mount_point[mount_length - 1] == '/' && suffix[0] == '/')
        suffix++;
    size_t suffix_length = strlen(suffix);
    int insert_slash = suffix[0] != '/' && mount_length > 0 && mount_point[mount_length - 1] != '/';
    if (mount_length + (size_t)insert_slash >= capacity ||
        suffix_length >= capacity - mount_length - (size_t)insert_slash)
        return 0;
    memcpy(out, mount_point, mount_length);
    if (insert_slash)
        out[mount_length++] = '/';
    memcpy(out + mount_length, suffix, suffix_length + 1u);
    return 1;
}

static int cgroup_read_memberships(const char *path,
                                   rt_machine_cgroup_memberships_t *memberships) {
    FILE *file = fopen(path, "r");
    if (!file)
        return 0;
    char line[RT_MACHINE_CGROUP_PATH_CAPACITY + 256];
    while (fgets(line, sizeof(line), file)) {
        if (!strchr(line, '\n') && !feof(file)) {
            int byte;
            while ((byte = fgetc(file)) != '\n' && byte != EOF) {
            }
            continue;
        }
        line[strcspn(line, "\r\n")] = '\0';
        char *first_colon = strchr(line, ':');
        char *second_colon = first_colon ? strchr(first_colon + 1, ':') : NULL;
        if (!first_colon || !second_colon || second_colon[1] != '/')
            continue;
        *second_colon = '\0';
        const char *controllers = first_colon + 1;
        const char *membership = second_colon + 1;
        if (controllers[0] == '\0') {
            (void)cgroup_copy(
                memberships->unified, sizeof(memberships->unified), membership);
            continue;
        }
        if (cgroup_has_token(controllers, "cpu"))
            (void)cgroup_copy(memberships->cpu, sizeof(memberships->cpu), membership);
        if (cgroup_has_token(controllers, "cpuset"))
            (void)cgroup_copy(memberships->cpuset, sizeof(memberships->cpuset), membership);
        if (cgroup_has_token(controllers, "memory"))
            (void)cgroup_copy(memberships->memory, sizeof(memberships->memory), membership);
    }
    fclose(file);
    return 1;
}

int zanna_machine_linux_resolve_cgroups(const char *mountinfo_path,
                                        const char *membership_path,
                                        rt_machine_linux_cgroup_paths_t *out) {
    if (!mountinfo_path || !membership_path || !out)
        return 0;
    memset(out, 0, sizeof(*out));
    rt_machine_cgroup_memberships_t memberships = {{0}, {0}, {0}, {0}};
    if (!cgroup_read_memberships(membership_path, &memberships))
        return 0;
    FILE *file = fopen(mountinfo_path, "r");
    if (!file)
        return 0;

    char line[RT_MACHINE_CGROUP_PATH_CAPACITY * 2];
    while (fgets(line, sizeof(line), file)) {
        if (!strchr(line, '\n') && !feof(file)) {
            int byte;
            while ((byte = fgetc(file)) != '\n' && byte != EOF) {
            }
            continue;
        }
        line[strcspn(line, "\r\n")] = '\0';
        char *separator = strstr(line, " - ");
        if (!separator)
            continue;
        *separator = '\0';
        char *after = separator + 3;
        char *save = NULL;
        char *field = strtok_r(line, " ", &save);
        int field_index = 0;
        char *encoded_root = NULL;
        char *encoded_point = NULL;
        while (field) {
            if (field_index == 3)
                encoded_root = field;
            else if (field_index == 4) {
                encoded_point = field;
                break;
            }
            field = strtok_r(NULL, " ", &save);
            field_index++;
        }
        char *after_save = NULL;
        char *filesystem = strtok_r(after, " ", &after_save);
        (void)strtok_r(NULL, " ", &after_save);
        char *super_options = strtok_r(NULL, " ", &after_save);
        if (!encoded_root || !encoded_point || !filesystem || !super_options)
            continue;
        char root[RT_MACHINE_CGROUP_PATH_CAPACITY];
        char point[RT_MACHINE_CGROUP_PATH_CAPACITY];
        if (!cgroup_decode_mount_path(encoded_root, root, sizeof(root)) ||
            !cgroup_decode_mount_path(encoded_point, point, sizeof(point)))
            continue;
        if (strcmp(filesystem, "cgroup2") == 0 && memberships.unified[0] != '\0') {
            (void)cgroup_join_membership(root,
                                        point,
                                        memberships.unified,
                                        out->unified,
                                        sizeof(out->unified));
        } else if (strcmp(filesystem, "cgroup") == 0) {
            if (memberships.cpu[0] && cgroup_has_token(super_options, "cpu"))
                (void)cgroup_join_membership(
                    root, point, memberships.cpu, out->cpu, sizeof(out->cpu));
            if (memberships.cpuset[0] && cgroup_has_token(super_options, "cpuset"))
                (void)cgroup_join_membership(
                    root, point, memberships.cpuset, out->cpuset, sizeof(out->cpuset));
            if (memberships.memory[0] && cgroup_has_token(super_options, "memory"))
                (void)cgroup_join_membership(
                    root, point, memberships.memory, out->memory, sizeof(out->memory));
        }
    }
    fclose(file);
    return out->unified[0] || out->cpu[0] || out->cpuset[0] || out->memory[0];
}

#else

int zanna_machine_linux_resolve_cgroups(const char *mountinfo_path,
                                        const char *membership_path,
                                        rt_machine_linux_cgroup_paths_t *out) {
    (void)mountinfo_path;
    (void)membership_path;
    (void)out;
    return 0;
}

#endif
