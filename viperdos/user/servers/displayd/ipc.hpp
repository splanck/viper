//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file ipc.hpp
 * @brief IPC protocol handlers for displayd.
 */

#pragma once

#include "types.hpp"

namespace displayd {

// Handle incoming client request
void handle_request(int32_t client_channel,
                    const uint8_t *data,
                    size_t len,
                    const uint32_t *handles,
                    uint32_t handle_count);

// Handle create surface request (called from handle_request)
void handle_create_surface(int32_t client_channel,
                           const uint8_t *data,
                           size_t len,
                           const uint32_t *handles,
                           uint32_t handle_count);

} // namespace displayd
