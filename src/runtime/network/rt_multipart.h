//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_multipart.h
// Purpose: Multipart form-data builder/parser for HTTP file uploads.
// Key invariants:
//   - Builder creates RFC 2046 compliant multipart/form-data bodies.
//   - Parser extracts fields and files from multipart bodies.
//   - Boundary is randomly generated for each builder instance.
// Ownership/Lifetime:
//   - Multipart objects are GC-managed via rt_obj_set_finalizer.
// Links: rt_network_http.c (consumer)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a new multipart form-data builder.
void *rt_multipart_new(void);

/// @brief Add a text field to the multipart body.
void *rt_multipart_add_field(void *mp, rt_string name, rt_string value);

/// @brief Add a file attachment to the multipart body.
void *rt_multipart_add_file(void *mp, rt_string name, rt_string filename, void *data);

/// @brief Get the Content-Type header value (includes boundary).
rt_string rt_multipart_content_type(void *mp);

/// @brief Build the complete multipart body as Bytes.
void *rt_multipart_build(void *mp);

/// @brief Get the number of parts.
int64_t rt_multipart_count(void *mp);

/// @brief Parse a multipart body given content-type and body bytes.
void *rt_multipart_parse(rt_string content_type, void *body);

/// @brief Get a field value by name from a parsed multipart.
rt_string rt_multipart_get_field(void *mp, rt_string name);

/// @brief Get a file's data by name from a parsed multipart.
void *rt_multipart_get_file(void *mp, rt_string name);

#ifdef __cplusplus
}
#endif
