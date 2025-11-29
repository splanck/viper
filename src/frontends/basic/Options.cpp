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

namespace il::frontends::basic
{

static bool g_enableRuntimeNamespaces = true;   // default ON
static bool g_enableRuntimeTypeBridging = true; // default ON for iteration
static bool g_enableSelectCaseConstLabels = true; // default ON for CONST labels in SELECT CASE

bool FrontendOptions::enableRuntimeNamespaces()
{
    return g_enableRuntimeNamespaces;
}

void FrontendOptions::setEnableRuntimeNamespaces(bool on)
{
    g_enableRuntimeNamespaces = on;
}

bool FrontendOptions::enableRuntimeTypeBridging()
{
    return g_enableRuntimeTypeBridging;
}

void FrontendOptions::setEnableRuntimeTypeBridging(bool on)
{
    g_enableRuntimeTypeBridging = on;
}

bool FrontendOptions::enableSelectCaseConstLabels()
{
    return g_enableSelectCaseConstLabels;
}

void FrontendOptions::setEnableSelectCaseConstLabels(bool on)
{
    g_enableSelectCaseConstLabels = on;
}

} // namespace il::frontends::basic
