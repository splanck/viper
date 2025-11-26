// File: src/runtime/rt_ns_bridge.h
// Purpose: Prototypes for runtime namespace bridging helpers.
// Notes: Supports namespaced type construction for OOP runtime classes.

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    // Allocate an opaque object instance for Viper.Text.StringBuilder.
    // Returns a pointer managed by the runtime object heap (refcounted).
    void *rt_ns_stringbuilder_new(void);

#ifdef __cplusplus
}
#endif
