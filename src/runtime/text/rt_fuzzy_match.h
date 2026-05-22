//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/text/rt_fuzzy_match.h
// Purpose: Reusable fuzzy matching and quick-open ranking helpers.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t rt_fuzzy_match_score(rt_string query, rt_string candidate);
void *rt_fuzzy_match_match(rt_string query, rt_string candidate);

#ifdef __cplusplus
}
#endif
