// File: src/runtime/rt_ns_bridge.h
// Purpose: Prototypes for minimal runtime namespace bridging helpers.
// Notes: Experimental helpers to support namespaced type construction.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Allocate an opaque object instance for Viper.System.Text.StringBuilder.
// Returns a pointer managed by the runtime object heap (refcounted).
void *rt_ns_stringbuilder_new(void);

#ifdef __cplusplus
}
#endif

