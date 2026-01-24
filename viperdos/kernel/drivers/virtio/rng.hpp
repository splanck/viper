//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/drivers/virtio/rng.hpp
// Purpose: Virtio RNG device interface (virtio-rng).
// Key invariants: Writable buffer filled with random bytes by host.
// Ownership/Lifetime: Singleton device; provides entropy for TLS/crypto.
// Links: kernel/drivers/virtio/rng.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../../include/types.hpp"
#include "virtio.hpp"
#include "virtqueue.hpp"

namespace virtio {
namespace rng {

/**
 * @file rng.hpp
 * @brief Virtio RNG device interface (virtio-rng).
 *
 * @details
 * Virtio-rng provides a simple entropy source backed by the host. The guest
 * submits a writable buffer descriptor and the device fills it with random
 * bytes. This module wraps that interaction and exposes an API to obtain random
 * bytes for cryptography and protocol needs (e.g. TLS).
 */

/**
 * @brief Initialize the virtio-rng device if present.
 *
 * @details
 * Probes for a virtio RNG device, sets it up, initializes its virtqueue, and
 * allocates a DMA buffer used for requests.
 *
 * @return `true` if the RNG device was found and initialized.
 */
bool init();

/**
 * @brief Check whether the RNG device is initialized and usable.
 *
 * @return `true` if available, otherwise `false`.
 */
bool is_available();

/**
 * @brief Retrieve random bytes from the virtio RNG device.
 *
 * @details
 * Requests up to `len` bytes, using repeated queue submissions if necessary.
 * The function may return fewer bytes than requested if descriptors are
 * exhausted or the device does not complete within the polling timeout.
 *
 * @param buffer Destination buffer.
 * @param len Number of bytes requested.
 * @return Number of bytes actually written to `buffer`.
 */
usize get_bytes(u8 *buffer, usize len);

} // namespace rng
} // namespace virtio
