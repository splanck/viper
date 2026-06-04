//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/repl/ReplOutputCapture.cpp
// Purpose: Implementation of scoped runtime stdout capture for REPL eval.
// Key invariants:
//   - The prior runtime output hook is restored exactly once.
//   - The runtime callback never owns the byte range it receives.
// Ownership/Lifetime:
//   - HookState is owned by ScopedReplOutputCapture and deleted in the destructor.
// Links: src/repl/ReplOutputCapture.hpp, src/runtime/core/rt_output.h
//
//===----------------------------------------------------------------------===//

#include "ReplOutputCapture.hpp"

#include "runtime/core/rt_output.h"

namespace viper::repl {

/// @brief Saved runtime output hook for one scoped capture.
/// @details The C runtime API returns a plain hook record. Keeping it in an
///          out-of-line state object avoids exposing runtime headers through
///          the public C++ REPL header.
struct ScopedReplOutputCapture::HookState {
    rt_output_capture_hook previous{};
};

void ScopedReplOutputCapture::captureCallback(const char *data, size_t len, void *ctx) noexcept {
    if (!ctx)
        return;
    try {
        static_cast<ScopedReplOutputCapture *>(ctx)->append(data, len);
    } catch (...) {
        // The runtime output hook has a C ABI. Preserve that boundary by
        // dropping bytes on allocation failure instead of propagating.
    }
}

ScopedReplOutputCapture::ScopedReplOutputCapture() : state_(new HookState) {
    state_->previous = rt_output_set_capture_hook(captureCallback, this);
}

ScopedReplOutputCapture::~ScopedReplOutputCapture() noexcept {
    if (state_) {
        rt_output_set_capture_hook(state_->previous.fn, state_->previous.ctx);
        delete state_;
        state_ = nullptr;
    }
}

void ScopedReplOutputCapture::append(const char *data, size_t len) {
    if (!data || len == 0)
        return;
    output_.append(data, len);
}

} // namespace viper::repl
