//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_xml_internal.h
// Purpose: Shared internal XML node representation for the XML runtime, used by
//   the parser/DOM (rt_xml.c) and the serializer (rt_xml_format.c).
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_xml.h" // rt_xml_node_type_t
#include "rt_string.h"

// Shared parse/validation helpers used by rt_xml_format.c (defined in rt_xml.c).
bool contains_invalid_xml_chars(const char *s, size_t len);
int decode_entity(const char *str, size_t len, char *out, size_t *consumed);

typedef struct xml_node {
    rt_xml_node_type_t type; ///< Node type
    rt_string tag;           ///< Tag name (elements only)
    rt_string content;       ///< Text content (text/comment/cdata)
    void *attributes;        ///< Map of attributes (elements only)
    void *children;          ///< Seq of child nodes
    struct xml_node *parent; ///< Parent node (weak reference)
} xml_node;
