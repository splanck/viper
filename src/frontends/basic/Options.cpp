//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/Options.cpp
// Purpose: Implements process-global BASIC frontend feature flags.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Options.hpp"

#include <atomic>

namespace il::frontends::basic
{

// All flags use std::atomic<bool> for thread-safe access. See Options.hpp
// for threading model documentation.
static std::atomic<bool> g_enableRuntimeNamespaces{true};
static std::atomic<bool> g_enableRuntimeTypeBridging{true};
static std::atomic<bool> g_enableSelectCaseConstLabels{true};

bool FrontendOptions::enableRuntimeNamespaces()
{
    return g_enableRuntimeNamespaces.load(std::memory_order_relaxed);
}

void FrontendOptions::setEnableRuntimeNamespaces(bool on)
{
    g_enableRuntimeNamespaces.store(on, std::memory_order_relaxed);
}

bool FrontendOptions::enableRuntimeTypeBridging()
{
    return g_enableRuntimeTypeBridging.load(std::memory_order_relaxed);
}

void FrontendOptions::setEnableRuntimeTypeBridging(bool on)
{
    g_enableRuntimeTypeBridging.store(on, std::memory_order_relaxed);
}

bool FrontendOptions::enableSelectCaseConstLabels()
{
    return g_enableSelectCaseConstLabels.load(std::memory_order_relaxed);
}

void FrontendOptions::setEnableSelectCaseConstLabels(bool on)
{
    g_enableSelectCaseConstLabels.store(on, std::memory_order_relaxed);
}

} // namespace il::frontends::basic
