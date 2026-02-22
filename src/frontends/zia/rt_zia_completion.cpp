//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_zia_completion.cpp
/// @brief extern "C" bridge between the Zia CompletionEngine and the Viper
///        runtime string API (rt_string).
///
/// Lives in fe_zia so it has access to ZiaCompletion.hpp.  The rt_string
/// functions (rt_string_cstr, rt_str_len, rt_string_from_bytes) are declared
/// via rt_string.h but implemented in viper_runtime; symbols resolve at final
/// link time when the executable links both fe_zia and viper_runtime.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/ZiaCompletion.hpp"
#include "runtime/core/rt_string.h"

#include <string>

using namespace il::frontends::zia;

namespace
{
// One singleton CompletionEngine per process.  The engine maintains a single-
// entry LRU parse cache keyed by source hash, so repeated calls for the same
// file content do not re-parse.
CompletionEngine s_engine;
} // namespace

extern "C"
{

rt_string rt_zia_complete(rt_string source, int64_t line, int64_t col)
{
    const char *src_cstr = source ? rt_string_cstr(source) : "";
    size_t      src_len  = source ? (size_t)rt_str_len(source) : 0;

    std::string sourceStr(src_cstr ? src_cstr : "", src_len);

    auto        items  = s_engine.complete(sourceStr, (int)line, (int)col);
    std::string result = serialize(items);

    return rt_string_from_bytes(result.c_str(), result.size());
}

void rt_zia_completion_clear_cache(void)
{
    s_engine.clearCache();
}

} // extern "C"
